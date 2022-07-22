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

#include "plugin/health_checker/health_checker.h"

#include <inttypes.h>
#include <stddef.h>

#include <map>
#include <set>
#include <string>
#include <utility>

#include "logger.h"
#include "plugin/circuit_breaker/chain.h"
#include "plugin/plugin_manager.h"
#include "polaris/config.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "utils/time_clock.h"

namespace polaris {

BaseHealthChecker::BaseHealthChecker() : timeout_ms_(0), retry_(0) {}

BaseHealthChecker::~BaseHealthChecker() {}

ReturnCode BaseHealthChecker::Init(Config* config, Context* /*context*/) {
  timeout_ms_ = config->GetMsOrDefault(HealthCheckerConfig::kTimeoutKey, HealthCheckerConfig::kTimeoutDefault);
  retry_ = config->GetIntOrDefault(HealthCheckerConfig::kRetryKey, HealthCheckerConfig::kRetryDefault);
  return kReturnOk;
}

ReturnCode BaseHealthChecker::DetectInstance(Instance& instance, DetectResult& detect_result) {
  int retry_cnt = 0;
  ReturnCode result = kReturnUnknownError;

  do {
    if (retry_cnt != 0) {
      POLARIS_LOG(LOG_ERROR, "health checker[%s] failed to detect instance [%s:%d], retry count %d", Name(),
                  instance.GetHost().c_str(), instance.GetPort(), retry_cnt);
    }

    // call DetectInstanceOnce at least once
    result = DetectInstanceOnce(instance, detect_result);
    if (result == kReturnOk) return result;

    ++retry_cnt;
  } while (retry_cnt <= retry_);
  return result;
}

HealthCheckerChainImpl::HealthCheckerChainImpl(const ServiceKey& service_key, LocalRegistry* local_registry) {
  service_key_ = service_key;
  local_registry_ = local_registry;
  when_ = "never";
  health_check_ttl_ms_ = 0;
  next_detect_time_ms_ = Time::GetCoarseSteadyTimeMs();
}

HealthCheckerChainImpl::~HealthCheckerChainImpl() {
  for (std::size_t i = 0; i < health_checker_list_.size(); ++i) {
    delete health_checker_list_[i];
  }
  health_checker_list_.clear();
  local_registry_ = nullptr;
}

ReturnCode HealthCheckerChainImpl::Init(Config* config, Context* context) {
  if (config->GetRootKey() == "outlierDetection") {
    if (!config->GetBoolOrDefault(HealthCheckerConfig::kChainEnableKey, HealthCheckerConfig::kChainEnableDefault)) {
      return kReturnOk;
    }
    // 兼容 outlier detection配置
    when_ = HealthCheckerConfig::kChainWhenOnRecover;
    health_check_ttl_ms_ = config->GetMsOrDefault(HealthCheckerConfig::kDetectorIntervalKey,
                                                  HealthCheckerConfig::kDetectorIntervalDefault);
  }
  if (config->GetRootKey() == "healthCheck") {
    when_ = config->GetStringOrDefault(HealthCheckerConfig::kChainWhenKey, HealthCheckerConfig::kChainWhenNever);
    if (when_ == HealthCheckerConfig::kChainWhenNever) {
      return kReturnOk;
    }
    health_check_ttl_ms_ =
        config->GetMsOrDefault(HealthCheckerConfig::kCheckerIntervalKey, HealthCheckerConfig::kDetectorIntervalDefault);
  }

  POLARIS_LOG(LOG_INFO, "health checker for service[%s/%s] is %s", service_key_.namespace_.c_str(),
              service_key_.name_.c_str(), when_.c_str());

  std::vector<std::string> plugin_name_list =
      config->GetListOrDefault(HealthCheckerConfig::kChainPluginListKey, HealthCheckerConfig::kChainPluginListDefault);
  if (plugin_name_list.empty()) {
    POLARIS_LOG(LOG_WARN, "enable health checker for service[%s/%s], but config [chain] not found",
                service_key_.name_.c_str(), service_key_.namespace_.c_str());
    when_ = HealthCheckerConfig::kChainWhenNever;
    return kReturnOk;
  }

  Config* chain_config = config->GetSubConfig("plugin");
  Config* plugin_config = nullptr;
  Plugin* plugin = nullptr;
  HealthChecker* health_checker;
  ReturnCode ret;
  for (size_t i = 0; i < plugin_name_list.size(); i++) {
    std::string& plugin_name = plugin_name_list[i];
    ret = PluginManager::Instance().GetPlugin(plugin_name, kPluginHealthChecker, plugin);
    if (ret == kReturnOk) {
      health_checker = dynamic_cast<HealthChecker*>(plugin);
      if (health_checker == nullptr) {
        continue;
      }
      plugin_config = chain_config->GetSubConfig(plugin_name);
      if (plugin_config == nullptr) {
        continue;
      }

      ret = health_checker->Init(plugin_config, context);
      if (ret == kReturnOk) {
        POLARIS_LOG(LOG_INFO, "Init health checker plugin[%s] for service[%s/%s] success", plugin_name.c_str(),
                    service_key_.namespace_.c_str(), service_key_.name_.c_str());
        health_checker_list_.push_back(health_checker);
      } else {
        POLARIS_LOG(LOG_ERROR, "Init health checker plugin[%s] for service[%s/%s] failed, skip it", plugin_name.c_str(),
                    service_key_.namespace_.c_str(), service_key_.name_.c_str());
        delete health_checker;
        health_checker = nullptr;
      }
      delete plugin_config;
      plugin_config = nullptr;
    } else {
      POLARIS_LOG(LOG_ERROR, "health checker plugin with name[%s] not found, skip it for service[%s/%s]",
                  plugin_name.c_str(), service_key_.namespace_.c_str(), service_key_.name_.c_str());
    }
  }
  delete chain_config;
  chain_config = nullptr;

  if (health_checker_list_.empty()) {
    POLARIS_LOG(LOG_ERROR,
                "The health checker of service[%s/%s] lost because health checker chain"
                " init failed",
                service_key_.namespace_.c_str(), service_key_.name_.c_str());
    when_ = HealthCheckerConfig::kChainWhenNever;
  }
  return kReturnOk;
}

ReturnCode HealthCheckerChainImpl::DetectInstance(CircuitBreakerChain& circuit_breaker_chain) {
  if (when_ == HealthCheckerConfig::kChainWhenNever) {
    return kReturnOk;
  }
  uint64_t steady_time_ms = Time::GetCoarseSteadyTimeMs();
  if (steady_time_ms <= next_detect_time_ms_) {
    return kReturnOk;
  }
  next_detect_time_ms_ = steady_time_ms + health_check_ttl_ms_;

  if (local_registry_ == nullptr) {
    POLARIS_LOG(LOG_ERROR, "The health checker local_registry_ of service[%s/%s] is null",
                service_key_.namespace_.c_str(), service_key_.name_.c_str());
    return kReturnOk;
  }

  ServiceData* service_data = nullptr;
  std::vector<Instance*> health_check_instances;
  if (local_registry_->GetCircuitBreakerInstances(service_key_, service_data, health_check_instances) != kReturnOk) {
    return kReturnOk;
  }

  ServiceInstances service_instances(service_data);
  std::map<std::string, Instance*>& instance_map = service_instances.GetInstances();

  if (when_ == HealthCheckerConfig::kChainWhenAlways) {
    health_check_instances.clear();
    // 健康检查设置为always, 则探测所有非隔离实例
    for (std::map<std::string, Instance*>::iterator instance_iter = instance_map.begin();
         instance_iter != instance_map.end(); ++instance_iter) {
      if (!instance_iter->second->isIsolate()) {
        health_check_instances.push_back(instance_iter->second);
      }
    }
  } else if (when_ != HealthCheckerConfig::kChainWhenOnRecover) {
    // 健康检查设置不为on_recover, 则探测半开实例
    health_check_instances.clear();
  }
  service_data->DecrementRef();

  POLARIS_LOG(LOG_DEBUG, "health check for service[%s/%s] with %zu instance", service_key_.namespace_.c_str(),
              service_key_.name_.c_str(), health_check_instances.size());

  for (std::size_t i = 0; i < health_check_instances.size(); ++i) {
    bool is_detect_success = false;
    Instance* instance = health_check_instances[i];
    for (std::size_t i = 0; i < health_checker_list_.size(); ++i) {
      HealthChecker*& detector = health_checker_list_[i];
      DetectResult detector_result;
      if (kReturnOk == detector->DetectInstance(*instance, detector_result)) {
        POLARIS_LOG(LOG_INFO,
                    "The detector[%s] of service[%s/%s] instance[%s-%s:%d] success, "
                    "elapsing %" PRIu64 " ms",
                    detector_result.detect_type.c_str(), service_key_.namespace_.c_str(), service_key_.name_.c_str(),
                    instance->GetId().c_str(), instance->GetHost().c_str(), instance->GetPort(),
                    detector_result.elapse);
        is_detect_success = true;
        break;
      } else {
        POLARIS_LOG(LOG_INFO,
                    "The detector[%s] of service[%s/%s] instance[%s-%s:%d] return[%d],"
                    " elapsing %" PRIu64 " ms",
                    detector_result.detect_type.c_str(), service_key_.namespace_.c_str(), service_key_.name_.c_str(),
                    instance->GetId().c_str(), instance->GetHost().c_str(), instance->GetPort(),
                    detector_result.return_code, detector_result.elapse);
      }
    }

    if (when_ == HealthCheckerConfig::kChainWhenAlways) {  // 一直探测节点状态
      if (is_detect_success) {
        if (circuit_breaker_chain.TranslateStatus(instance->GetId(), kCircuitBreakerOpen, kCircuitBreakerClose)) {
          POLARIS_LOG(LOG_INFO, "service[%s/%s] instance[%s-%s:%d] open to close", service_key_.namespace_.c_str(),
                      service_key_.name_.c_str(), instance->GetId().c_str(), instance->GetHost().c_str(),
                      instance->GetPort());
        }
      } else {
        if (circuit_breaker_chain.TranslateStatus(instance->GetId(), kCircuitBreakerClose, kCircuitBreakerOpen)) {
          POLARIS_LOG(LOG_INFO, "service[%s/%s] instance[%s-%s:%d] close to open", service_key_.namespace_.c_str(),
                      service_key_.name_.c_str(), instance->GetId().c_str(), instance->GetHost().c_str(),
                      instance->GetPort());
        }
      }
    } else {  // 只在熔断恢复时探测成功时转化为半开状态
      if (is_detect_success) {
        if (circuit_breaker_chain.TranslateStatus(instance->GetId(), kCircuitBreakerOpen, kCircuitBreakerHalfOpen)) {
          POLARIS_LOG(LOG_INFO, "service[%s/%s] instance[%s-%s:%d] close to half open", service_key_.namespace_.c_str(),
                      service_key_.name_.c_str(), instance->GetId().c_str(), instance->GetHost().c_str(),
                      instance->GetPort());
        }
      }
    }
  }
  return kReturnOk;
}

std::vector<HealthChecker*> HealthCheckerChainImpl::GetHealthCheckers() { return health_checker_list_; }

}  // namespace polaris
