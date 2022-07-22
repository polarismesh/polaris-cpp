//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
//  language governing permissions and limitations under the License.
//

#include "plugin/circuit_breaker/chain.h"

#include <stddef.h>

#include <memory>
#include <set>
#include <utility>

#include "cache/service_cache.h"
#include "context/context_impl.h"
#include "logger.h"
#include "monitor/service_record.h"
#include "plugin/circuit_breaker/set_circuit_breaker.h"
#include "plugin/health_checker/health_checker.h"
#include "plugin/plugin_manager.h"
#include "polaris/config.h"
#include "polaris/log.h"
#include "polaris/model.h"
#include "utils/time_clock.h"

namespace polaris {

CircuitBreakerChainData::CircuitBreakerChainData() : last_update_version_(0), current_version_(0) {}

CircuitBreakerChainData::~CircuitBreakerChainData() {}

void CircuitBreakerChainData::AppendPluginData(const CircuitBreakerPluginData& plugin_data) {
  plugin_data_map_.push_back(plugin_data);
}

// 转换熔断器状态
// 规则：
//   1. 只能有一个熔断器去触发熔断，当实例已被某个熔断器熔断时，即使别的熔断器符合熔断条件也不再触发
//   2. 恢复时只能由熔断的熔断器去触发半开恢复，半开恢复也只能由该熔断器恢复到关闭状态
//   3. 所以熔断的时候实例的状态必须在正常状态，实例不再正常状态，熔断器仍然需要记录统计数据
CircuitChangeRecord* CircuitBreakerChainData::TranslateStatus(int plugin_index, const std::string& instance_id,
                                                              CircuitBreakerStatus from_status,
                                                              CircuitBreakerStatus to_status) {
  const std::lock_guard<std::mutex> mutex_guard(lock_);  // 所有修改在此处加锁进行互斥
  // 检查是否要更新总状态
  CircuitBreakerChainStatus& chain_status = chain_status_map_[instance_id];
  if (chain_status.owner_plugin_index != 0 && chain_status.owner_plugin_index != plugin_index) {
    return nullptr;
  }
  std::string plugin_name = plugin_data_map_[plugin_index - 1].plugin_name;
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    POLARIS_LOG(LOG_TRACE, "plugin[%s] try to translate circuit breaker status for instance[%s] from[%s] to status[%s]",
                plugin_name.c_str(), instance_id.c_str(), CircuitBreakerStatusToStr(from_status),
                CircuitBreakerStatusToStr(to_status));
  }
  if (chain_status.status != from_status) {
    POLARIS_LOG(LOG_TRACE, "circuit breaker status[%s] for instance[%s] not in src status[%s]",
                CircuitBreakerStatusToStr(chain_status.status), instance_id.c_str(),
                CircuitBreakerStatusToStr(from_status));
    return nullptr;
  }
  if (chain_status.status == to_status) {
    POLARIS_LOG(LOG_TRACE, "circuit breaker status instance[%s] already in dest status[%s]", instance_id.c_str(),
                CircuitBreakerStatusToStr(to_status));
    return nullptr;
  }

  CircuitChangeRecord* record = new CircuitChangeRecord();
  record->change_time_ = Time::GetSystemTimeMs();
  record->change_seq_ = ++chain_status.change_seq_id;
  record->from_ = from_status;
  record->to_ = to_status;
  record->reason_ = plugin_name;
  // 只在关闭时删除数，长期不会访问过期时会被转换到关闭状态
  if (to_status == kCircuitBreakerClose) {
    chain_status_map_.erase(instance_id);
  } else {
    chain_status.status = to_status;
    chain_status.owner_plugin_index = plugin_index;
  }
  current_version_++;
  return record;
}

bool CircuitBreakerChainData::CheckAndSyncToLocalRegistry(LocalRegistry* local_registry,
                                                          const ServiceKey& service_key) {
  if (last_update_version_ == current_version_) {
    return false;
  }
  const std::lock_guard<std::mutex> mutex_guard(lock_);
  if (last_update_version_ == current_version_) {  // double check
    return false;
  }
  CircuitBreakerData result;
  result.version = current_version_;
  std::map<std::string, CircuitBreakerChainStatus>::iterator chain_it;
  for (chain_it = chain_status_map_.begin(); chain_it != chain_status_map_.end(); ++chain_it) {
    if (chain_it->second.status == kCircuitBreakerOpen) {
      result.open_instances.insert(chain_it->first);
    } else if (chain_it->second.status == kCircuitBreakerHalfOpen) {
      int request_count = plugin_data_map_[chain_it->second.owner_plugin_index - 1].request_after_half_open;
      result.half_open_instances[chain_it->first] = request_count;
    }
  }
  POLARIS_LOG(LOG_DEBUG, "Update circuit breaker status for service[%s/%s]", service_key.namespace_.c_str(),
              service_key.name_.c_str());
  local_registry->UpdateCircuitBreakerData(service_key, result);
  last_update_version_ = current_version_;
  return true;
}

InstancesCircuitBreakerStatus::InstancesCircuitBreakerStatus(CircuitBreakerChainData* chain_data, int plugin_index,
                                                             ServiceKey& service_key, ServiceRecord* service_record,
                                                             bool auto_half_open_enable)
    : service_key_(service_key),
      service_record_(service_record),
      chain_data_(chain_data),
      plugin_index_(plugin_index),
      auto_half_open_enable_(auto_half_open_enable) {}

InstancesCircuitBreakerStatus::~InstancesCircuitBreakerStatus() {
  chain_data_ = nullptr;
  service_record_ = nullptr;
}

bool InstancesCircuitBreakerStatus::TranslateStatus(const std::string& instance_id, CircuitBreakerStatus from_status,
                                                    CircuitBreakerStatus to_status) {
  CircuitChangeRecord* record = chain_data_->TranslateStatus(plugin_index_, instance_id, from_status, to_status);
  if (record != nullptr) {
    service_record_->InstanceCircuitBreak(service_key_, instance_id, record);
  }
  return record != nullptr;
}

CircuitBreakerChain::CircuitBreakerChain(const ServiceKey& service_key)
    : service_key_(service_key),
      context_(nullptr),
      enable_(false),
      check_period_(0),
      next_check_time_(0),
      circuit_breaker_list_(0),
      chain_data_(new CircuitBreakerChainData()),
      set_circuit_breaker_(nullptr) {}

CircuitBreakerChain::~CircuitBreakerChain() {
  for (std::size_t i = 0; i < circuit_breaker_list_.size(); i++) {
    delete circuit_breaker_list_[i];
  }
  circuit_breaker_list_.clear();
  for (std::size_t i = 0; i < instances_status_list_.size(); i++) {
    delete instances_status_list_[i];
  }
  instances_status_list_.clear();
  if (chain_data_ != nullptr) {
    delete chain_data_;
  }
  if (set_circuit_breaker_ != nullptr) {
    delete set_circuit_breaker_;
    set_circuit_breaker_ = nullptr;
  }
}

ReturnCode CircuitBreakerChain::InitPlugin(Config* config, Context* context, const std::string& plugin_name) {
  Plugin* plugin = nullptr;
  ReturnCode ret = PluginManager::Instance().GetPlugin(plugin_name, kPluginCircuitBreaker, plugin);
  if (ret != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "circuit breaker plugin with name[%s] for service[%s/%s] not found", plugin_name.c_str(),
                service_key_.namespace_.c_str(), service_key_.name_.c_str());
    return ret;
  }
  CircuitBreaker* circuit_breaker = dynamic_cast<CircuitBreaker*>(plugin);
  if (circuit_breaker == nullptr) {
    POLARIS_LOG(LOG_ERROR, "plugin with name[%s] and type[%s] for service[%s/%s] can not convert to circuit breaker",
                plugin_name.c_str(), PluginTypeToString(kPluginCircuitBreaker), service_key_.namespace_.c_str(),
                service_key_.name_.c_str());
    delete plugin;
    return kReturnInvalidConfig;
  }
  circuit_breaker_list_.push_back(circuit_breaker);
  if ((ret = circuit_breaker->Init(config, context)) != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "init circuit breaker plugin[%s] for service[%s/%s] failed", plugin_name.c_str(),
                service_key_.namespace_.c_str(), service_key_.name_.c_str());
  } else {
    int request_after_half_open = circuit_breaker->RequestAfterHalfOpen();
    if (request_after_half_open > 0) {
      CircuitBreakerPluginData plugin_data = {plugin_name, request_after_half_open};
      chain_data_->AppendPluginData(plugin_data);
      instances_status_list_.push_back(new InstancesCircuitBreakerStatus(
          chain_data_, circuit_breaker_list_.size(), service_key_, context->GetContextImpl()->GetServiceRecord(),
          health_check_when_ == HealthCheckerConfig::kChainWhenNever));
    } else {
      POLARIS_LOG(LOG_ERROR, "request after half-open of service[%s/%s] for plugin[%s] must big than 0",
                  plugin_name.c_str(), service_key_.namespace_.c_str(), service_key_.name_.c_str());
      ret = kReturnInvalidConfig;
    }
  }
  return ret;
}

ReturnCode CircuitBreakerChain::Init(Config* config, Context* context, const std::string& health_check_when) {
  static const char kChainEnableKey[] = "enable";
  static const bool kChainEnableDefault = true;
  enable_ = config->GetBoolOrDefault(kChainEnableKey, kChainEnableDefault);
  if (enable_ == false) {
    POLARIS_LOG(LOG_INFO, "circuit breaker for service[%s/%s] is disable", service_key_.namespace_.c_str(),
                service_key_.name_.c_str());
    return kReturnOk;
  }

  static const char kChainCheckPeriodKey[] = "checkPeriod";
  static const uint64_t kChainCheckPeriodDefault = 1000;
  check_period_ = config->GetMsOrDefault(kChainCheckPeriodKey, kChainCheckPeriodDefault);
  POLARIS_CHECK(check_period_ >= 100, kReturnInvalidConfig);
  context_ = context;
  health_check_when_ = health_check_when;

  static const char kChainPluginListKey[] = "chain";
  static const char kChainPluginListDefault[] = "errorCount, errorRate";
  std::vector<std::string> plugin_name_list = config->GetListOrDefault(kChainPluginListKey, kChainPluginListDefault);
  if (plugin_name_list.empty()) {
    POLARIS_LOG(LOG_WARN, "circuit breaker config[enable] for service[%s/%s] is true, but config [chain] not found",
                service_key_.namespace_.c_str(), service_key_.name_.c_str());
    return kReturnInvalidConfig;
  }

  Config* chain_config = config->GetSubConfig("plugin");
  ReturnCode ret = kReturnOk;
  for (size_t i = 0; i < plugin_name_list.size(); i++) {
    std::string& plugin_name = plugin_name_list[i];
    Config* plugin_config = chain_config->GetSubConfig(plugin_name);
    ret = InitPlugin(plugin_config, context, plugin_name);
    delete plugin_config;
    if (ret != kReturnOk) {
      delete chain_config;
      return ret;
    }
  }
  delete chain_config;

  Config* set_config = config->GetSubConfig("setCircuitBreaker");
  set_circuit_breaker_ = new SetCircuitBreakerImpl(service_key_);
  if (set_circuit_breaker_->Init(set_config, context) != kReturnOk) {
    delete set_circuit_breaker_;
    set_circuit_breaker_ = nullptr;
  }
  delete set_config;

  if (health_check_when_ == HealthCheckerConfig::kChainWhenAlways) {
    CircuitBreakerPluginData plugin_data = {"health_check", 0};
    chain_data_->AppendPluginData(plugin_data);
    instances_status_list_.push_back(new InstancesCircuitBreakerStatus(
        chain_data_, circuit_breaker_list_.size() + 1, service_key_, context->GetContextImpl()->GetServiceRecord(),
        health_check_when_ != HealthCheckerConfig::kChainWhenNever));
  }

  return ret;
}

ReturnCode CircuitBreakerChain::RealTimeCircuitBreak(const InstanceGauge& instance_gauge) {
  if (enable_ == false) {
    return kReturnOk;
  }

  // 执行实时熔断链
  for (std::size_t i = 0; i < circuit_breaker_list_.size(); ++i) {
    CircuitBreaker* circuit_breaker = circuit_breaker_list_[i];
    circuit_breaker->RealTimeCircuitBreak(instance_gauge, instances_status_list_[i]);
  }

  // 版本变更触发熔断状态更新到本地缓存
  if (chain_data_->CheckAndSyncToLocalRegistry(context_->GetLocalRegistry(), service_key_)) {
    this->SubmitUpdateCache(chain_data_->GetCurrentVersion());
  }

  if (set_circuit_breaker_ != nullptr) {
    set_circuit_breaker_->RealTimeCircuitBreak(instance_gauge);
  }
  return kReturnOk;
}

ReturnCode CircuitBreakerChain::TimingCircuitBreak(InstanceExistChecker& exist_checker) {
  if (enable_ == false || Time::GetCoarseSteadyTimeMs() < next_check_time_) {
    return kReturnOk;
  }

  // 执行定时熔断链
  for (std::size_t i = 0; i < circuit_breaker_list_.size(); ++i) {
    CircuitBreaker* circuit_breaker = circuit_breaker_list_[i];
    circuit_breaker->TimingCircuitBreak(instances_status_list_[i]);
    circuit_breaker->CleanStatus(instances_status_list_[i], exist_checker);
  }
  if (chain_data_->CheckAndSyncToLocalRegistry(context_->GetLocalRegistry(), service_key_)) {
    this->SubmitUpdateCache(chain_data_->GetCurrentVersion());
  }
  next_check_time_ = Time::GetCoarseSteadyTimeMs() + check_period_;

  if (set_circuit_breaker_ != nullptr) {
    this->PrepareServicePbConfTrigger();  // 触发（非阻塞）拉取熔断配置
    set_circuit_breaker_->TimingCircuitBreak();
  }
  return kReturnOk;
}

std::vector<CircuitBreaker*> CircuitBreakerChain::GetCircuitBreakers() { return circuit_breaker_list_; }

bool CircuitBreakerChain::TranslateStatus(const std::string& instance_id, CircuitBreakerStatus from_status,
                                          CircuitBreakerStatus to_status) {
  if (from_status == kCircuitBreakerClose && to_status == kCircuitBreakerOpen) {
    if (instances_status_list_.size() > circuit_breaker_list_.size()) {
      // 最后一个状态为 健康检查 控制
      return instances_status_list_[circuit_breaker_list_.size()]->TranslateStatus(instance_id, from_status, to_status);
    }
    return false;
  }
  bool translate = false;
  for (std::size_t i = 0; i < instances_status_list_.size(); ++i) {
    if (instances_status_list_[i]->TranslateStatus(instance_id, from_status, to_status)) {
      translate = true;
      if (i < circuit_breaker_list_.size() && from_status == kCircuitBreakerOpen &&
          to_status == kCircuitBreakerHalfOpen) {
        circuit_breaker_list_[i]->DetectToHalfOpen(instance_id);
      }
    }
  }
  return translate;
}

void CircuitBreakerChain::SubmitUpdateCache(uint64_t circuit_breaker_version) {
  ContextImpl* context_impl = context_->GetContextImpl();
  CacheManager* cache_manager = context_impl->GetCacheManager();
  cache_manager->GetReactor().SubmitTask(
      new ServiceCacheUpdateTask(service_key_, circuit_breaker_version, context_impl));
}

void CircuitBreakerChain::PrepareServicePbConfTrigger() {
  ServiceData* service_data = nullptr;
  LocalRegistry* local_registry = context_->GetLocalRegistry();
  ReturnCode ret_code = local_registry->GetServiceDataWithRef(service_key_, kCircuitBreakerConfig, service_data);
  if (ret_code != kReturnOk) {
    ServiceDataNotify* conf_notify_tmp = nullptr;
    ret_code =
        local_registry->LoadServiceDataWithNotify(service_key_, kCircuitBreakerConfig, service_data, conf_notify_tmp);
    if (ret_code != kReturnOk) {
      POLARIS_LOG(POLARIS_WARN, "loading circuit breaker config for service[%s/%s] with error:%s",
                  service_key_.namespace_.c_str(), service_key_.name_.c_str(), ReturnCodeToMsg(ret_code).c_str());
    }
  }
  if (service_data != nullptr) {
    service_data->DecrementRef();
  }
}

}  // namespace polaris
