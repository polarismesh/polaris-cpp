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

#include "plugin/circuit_breaker/circuit_breaker.h"

#include <stddef.h>

#include <memory>
#include <set>
#include <utility>

#include "context_internal.h"
#include "logger.h"
#include "monitor/service_record.h"
#include "plugin/circuit_breaker/set_circuit_breaker.h"
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
//   1.
//   只能有一个熔断器去触发实例的熔断，当实例已经有一个熔断熔断的情况下，即使别的熔断器符合熔断条件也不再进行熔断
//   2. 恢复时只能由熔断的熔断器去触发半开恢复，半开恢复也只能由该熔断器恢复到关闭状态
//   3. 所以熔断的时候实例的状态必须在正常状态，实例不再正常状态，熔断器仍然需要记录统计数据
CircuitChangeRecord* CircuitBreakerChainData::TranslateStatus(int plugin_index,
                                                              const std::string& instance_id,
                                                              CircuitBreakerStatus from_status,
                                                              CircuitBreakerStatus to_status) {
  sync::MutexGuard mutex_guard(lock_);  // 所有修改在此处加锁进行互斥
  // 检查是否要更新总状态
  CircuitBreakerChainStatus& chain_status = chain_status_map_[instance_id];
  if (chain_status.owner_plugin_index != 0 && chain_status.owner_plugin_index != plugin_index) {
    return NULL;
  }
  std::string plugin_name = plugin_data_map_[plugin_index - 1].plugin_name;
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    POLARIS_LOG(LOG_TRACE,
                "plugin[%s] try to translate circuit breaker status for"
                " instance[%s] from[%s] to status[%s]",
                plugin_name.c_str(), instance_id.c_str(), CircuitBreakerStatusToStr(from_status),
                CircuitBreakerStatusToStr(to_status));
  }
  if (chain_status.status != from_status) {
    POLARIS_LOG(LOG_TRACE, "circuit breaker status[%s] for instance[%s] not in src status[%s]",
                CircuitBreakerStatusToStr(chain_status.status), instance_id.c_str(),
                CircuitBreakerStatusToStr(from_status));
    return NULL;
  }
  if (chain_status.status == to_status) {
    POLARIS_LOG(LOG_TRACE, "circuit breaker status instance[%s] already in dest status[%s]",
                instance_id.c_str(), CircuitBreakerStatusToStr(to_status));
    return NULL;
  }

  CircuitChangeRecord* record = new CircuitChangeRecord();
  record->change_time_        = Time::GetCurrentTimeMs();
  record->change_seq_         = ++chain_status.change_seq_id;
  record->from_               = from_status;
  record->to_                 = to_status;
  record->reason_             = plugin_name;
  // 只在关闭时删除数，长期不会访问过期时会被转换到关闭状态
  if (to_status == kCircuitBreakerClose) {
    chain_status_map_.erase(instance_id);
  } else {
    chain_status.status             = to_status;
    chain_status.owner_plugin_index = plugin_index;
  }
  current_version_++;
  return record;
}

void CircuitBreakerChainData::CheckAndSyncToLocalRegistry(LocalRegistry* local_registry,
                                                          const ServiceKey& service_key) {
  if (last_update_version_ == current_version_) {
    return;
  }
  sync::MutexGuard mutex_guard(lock_);
  // double check
  if (last_update_version_ == current_version_) {
    return;
  }
  CircuitBreakerData result;
  result.version = current_version_;
  std::map<std::string, CircuitBreakerChainStatus>::iterator chain_it;
  for (chain_it = chain_status_map_.begin(); chain_it != chain_status_map_.end(); ++chain_it) {
    if (chain_it->second.status == kCircuitBreakerOpen) {
      result.open_instances.insert(chain_it->first);
    } else if (chain_it->second.status == kCircuitBreakerHalfOpen) {
      int request_count =
          plugin_data_map_[chain_it->second.owner_plugin_index - 1].request_after_half_open;
      result.half_open_instances[chain_it->first] = request_count;
    }
  }
  POLARIS_LOG(LOG_DEBUG, "Update circuit breaker status for service[%s/%s]",
              service_key.namespace_.c_str(), service_key.name_.c_str());
  local_registry->UpdateCircuitBreakerData(service_key, result);
  last_update_version_ = current_version_;
}

InstancesCircuitBreakerStatusImpl::InstancesCircuitBreakerStatusImpl(
    CircuitBreakerChainData* chain_data, int plugin_index, ServiceKey& service_key,
    ServiceRecord* service_record, bool auto_half_open_enable)
    : service_key_(service_key) {
  chain_data_            = chain_data;
  plugin_index_          = plugin_index;
  service_record_        = service_record;
  auto_half_open_enable_ = auto_half_open_enable;
}

InstancesCircuitBreakerStatusImpl::~InstancesCircuitBreakerStatusImpl() {
  chain_data_     = NULL;
  service_record_ = NULL;
}

bool InstancesCircuitBreakerStatusImpl::TranslateStatus(const std::string& instance_id,
                                                        CircuitBreakerStatus from_status,
                                                        CircuitBreakerStatus to_status) {
  CircuitChangeRecord* record =
      chain_data_->TranslateStatus(plugin_index_, instance_id, from_status, to_status);
  if (record != NULL) {
    service_record_->InstanceCircuitBreak(service_key_, instance_id, record);
  }
  return record != NULL;
}

CircuitBreakerChainImpl::CircuitBreakerChainImpl(const ServiceKey& service_key,
                                                 LocalRegistry* local_registry,
                                                 bool auto_half_open_enable) {
  service_key_           = service_key;
  enable_                = false;
  check_period_          = 0;
  last_check_time_       = 0;
  local_registry_        = local_registry;
  chain_data_            = new CircuitBreakerChainData();
  auto_half_open_enable_ = auto_half_open_enable;
  set_circuit_breaker_   = NULL;
}

CircuitBreakerChainImpl::~CircuitBreakerChainImpl() {
  for (std::size_t i = 0; i < circuit_breaker_list_.size(); i++) {
    delete circuit_breaker_list_[i];
  }
  circuit_breaker_list_.clear();
  for (std::size_t i = 0; i < instances_status_list_.size(); i++) {
    delete instances_status_list_[i];
  }
  instances_status_list_.clear();
  if (chain_data_ != NULL) {
    delete chain_data_;
  }
  if (set_circuit_breaker_ != NULL) {
    delete set_circuit_breaker_;
    set_circuit_breaker_ = NULL;
  }
}

ReturnCode CircuitBreakerChainImpl::InitPlugin(Config* config, Context* context,
                                               const std::string& plugin_name) {
  Plugin* plugin = NULL;
  ReturnCode ret = PluginManager::Instance().GetPlugin(plugin_name, kPluginCircuitBreaker, plugin);
  if (ret != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "circuit breaker plugin with name[%s] for service[%s/%s] not found",
                plugin_name.c_str(), service_key_.namespace_.c_str(), service_key_.name_.c_str());
    return ret;
  }
  CircuitBreaker* circuit_breaker = dynamic_cast<CircuitBreaker*>(plugin);
  if (circuit_breaker == NULL) {
    POLARIS_LOG(LOG_ERROR,
                "plugin with name[%s] and type[%s] for service[%s/%s] can not "
                "convert to circuit breaker",
                plugin_name.c_str(), PluginTypeToString(kPluginCircuitBreaker),
                service_key_.namespace_.c_str(), service_key_.name_.c_str());
    delete plugin;
    return kReturnInvalidConfig;
  }
  circuit_breaker_list_.push_back(circuit_breaker);
  if ((ret = circuit_breaker->Init(config, context)) != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "init circuit breaker plugin[%s] for service[%s/%s] failed",
                plugin_name.c_str(), service_key_.namespace_.c_str(), service_key_.name_.c_str());
  } else {
    int request_after_half_open = circuit_breaker->RequestAfterHalfOpen();
    if (request_after_half_open > 0) {
      CircuitBreakerPluginData plugin_data = {plugin_name, request_after_half_open};
      chain_data_->AppendPluginData(plugin_data);
      instances_status_list_.push_back(new InstancesCircuitBreakerStatusImpl(
          chain_data_, circuit_breaker_list_.size(), service_key_,
          context->GetContextImpl()->GetServiceRecord(), auto_half_open_enable_));
    } else {
      POLARIS_LOG(LOG_ERROR,
                  "request after half-open of service[%s/%s] for plugin[%s] must big than 0",
                  plugin_name.c_str(), service_key_.namespace_.c_str(), service_key_.name_.c_str());
      ret = kReturnInvalidConfig;
    }
  }
  return ret;
}

ReturnCode CircuitBreakerChainImpl::Init(Config* config, Context* context) {
  enable_ = config->GetBoolOrDefault(CircuitBreakerConfig::kChainEnableKey,
                                     CircuitBreakerConfig::kChainEnableDefault);
  if (enable_ == false) {
    POLARIS_LOG(LOG_INFO, "circuit breaker for service[%s/%s] is disable",
                service_key_.namespace_.c_str(), service_key_.name_.c_str());
    return kReturnOk;
  }
  check_period_ = config->GetMsOrDefault(CircuitBreakerConfig::kChainCheckPeriodKey,
                                         CircuitBreakerConfig::kChainCheckPeriodDefault);
  POLARIS_CHECK(check_period_ >= 100, kReturnInvalidConfig);

  std::vector<std::string> plugin_name_list = config->GetListOrDefault(
      CircuitBreakerConfig::kChainPluginListKey, CircuitBreakerConfig::kChainPluginListDefault);
  if (plugin_name_list.empty()) {
    POLARIS_LOG(LOG_WARN,
                "circuit breaker config[enable] for service[%s/%s] is true, "
                "but config [chain] not found",
                service_key_.namespace_.c_str(), service_key_.name_.c_str());
    return kReturnInvalidConfig;
  }

  Config* chain_config = config->GetSubConfig("plugin");
  ReturnCode ret       = kReturnOk;
  for (size_t i = 0; i < plugin_name_list.size(); i++) {
    std::string& plugin_name = plugin_name_list[i];
    Config* plugin_config    = chain_config->GetSubConfig(plugin_name);
    ret                      = InitPlugin(plugin_config, context, plugin_name);
    delete plugin_config;
    if (ret != kReturnOk) {
      break;
    }
  }
  delete chain_config;
  if (ret != kReturnOk) {
    return ret;
  }

  Config* set_config   = config->GetSubConfig("setCircuitBreaker");
  set_circuit_breaker_ = new SetCircuitBreakerImpl(service_key_);
  if (set_circuit_breaker_->Init(set_config, context) != kReturnOk) {
    delete set_circuit_breaker_;
    set_circuit_breaker_ = NULL;
  }
  delete set_config;
  return ret;
}

ReturnCode CircuitBreakerChainImpl::RealTimeCircuitBreak(const InstanceGauge& instance_gauge) {
  if (enable_ == false) {
    return kReturnOk;
  }

  // 执行实时熔断链
  for (std::size_t i = 0; i < circuit_breaker_list_.size(); ++i) {
    CircuitBreaker* circuit_breaker = circuit_breaker_list_[i];
    circuit_breaker->RealTimeCircuitBreak(instance_gauge, instances_status_list_[i]);
  }

  // 版本变更触发熔断状态更新到本地缓存
  chain_data_->CheckAndSyncToLocalRegistry(local_registry_, service_key_);

  if (set_circuit_breaker_ != NULL) {
    set_circuit_breaker_->RealTimeCircuitBreak(instance_gauge);
  }
  return kReturnOk;
}

ReturnCode CircuitBreakerChainImpl::TimingCircuitBreak() {
  if (enable_ == false || Time::GetCurrentTimeMs() < last_check_time_ + check_period_) {
    return kReturnOk;
  }

  // 执行定时熔断链
  for (std::size_t i = 0; i < circuit_breaker_list_.size(); ++i) {
    CircuitBreaker* circuit_breaker = circuit_breaker_list_[i];
    circuit_breaker->TimingCircuitBreak(instances_status_list_[i]);
  }
  chain_data_->CheckAndSyncToLocalRegistry(local_registry_, service_key_);
  last_check_time_ = Time::GetCurrentTimeMs();

  if (set_circuit_breaker_ != NULL) {
    set_circuit_breaker_->TimingCircuitBreak();
  }
  return kReturnOk;
}

std::vector<CircuitBreaker*> CircuitBreakerChainImpl::GetCircuitBreakers() {
  return circuit_breaker_list_;
}

ReturnCode CircuitBreakerChainImpl::TranslateStatus(const std::string& instance_id,
                                                    CircuitBreakerStatus from_status,
                                                    CircuitBreakerStatus to_status) {
  for (std::size_t i = 0; i < instances_status_list_.size(); ++i) {
    instances_status_list_[i]->TranslateStatus(instance_id, from_status, to_status);
  }
  return kReturnOk;
}

void CircuitBreakerChainImpl::PrepareServicePbConfTrigger() {
  if (set_circuit_breaker_ == NULL) {
    return;
  }
  ServiceData* service_data = NULL;
  ReturnCode ret_code =
      local_registry_->GetServiceDataWithRef(service_key_, kCircuitBreakerConfig, service_data);
  if (ret_code != kReturnOk) {
    ServiceDataNotify* conf_notify_tmp = NULL;
    ret_code = local_registry_->LoadServiceDataWithNotify(service_key_, kCircuitBreakerConfig,
                                                          service_data, conf_notify_tmp);
    if (ret_code != kReturnOk) {
      POLARIS_LOG(POLARIS_WARN, "loading circuit breaker config for service[%s/%s] with error:%s",
                  service_key_.namespace_.c_str(), service_key_.name_.c_str(),
                  ReturnCodeToMsg(ret_code).c_str());
    }
  }
  if (service_data != NULL) {
    service_data->DecrementRef();
  }
}

}  // namespace polaris
