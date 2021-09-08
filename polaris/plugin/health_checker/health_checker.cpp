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
#include "plugin/plugin_manager.h"
#include "polaris/config.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "utils/time_clock.h"

namespace polaris {

HealthCheckerChainImpl::HealthCheckerChainImpl(const ServiceKey& service_key,
                                               LocalRegistry* local_registry) {
  service_key_         = service_key;
  local_registry_      = local_registry;
  when_                = "never";
  health_check_ttl_ms_ = 0;
  last_detect_time_ms_ = Time::GetCurrentTimeMs();
}

HealthCheckerChainImpl::~HealthCheckerChainImpl() {
  for (std::size_t i = 0; i < health_checker_list_.size(); ++i) {
    delete health_checker_list_[i];
  }
  health_checker_list_.clear();
  local_registry_ = NULL;
}

ReturnCode HealthCheckerChainImpl::Init(Config* config, Context* context) {
  when_ = config->GetStringOrDefault(HealthCheckerConfig::kChainWhenKey,
                                     HealthCheckerConfig::kChainWhenNever);
  if (when_ == HealthCheckerConfig::kChainWhenNever) {
    return kReturnOk;
  }
  POLARIS_LOG(LOG_INFO, "health checker for service[%s/%s] is enable",
              service_key_.namespace_.c_str(), service_key_.name_.c_str());

  health_check_ttl_ms_ = config->GetMsOrDefault(HealthCheckerConfig::kCheckerIntervalKey,
                                                HealthCheckerConfig::kDetectorIntervalDefault);

  std::vector<std::string> plugin_name_list = config->GetListOrDefault(
      HealthCheckerConfig::kChainPluginListKey, HealthCheckerConfig::kChainPluginListDefault);
  if (plugin_name_list.empty()) {
    POLARIS_LOG(LOG_WARN,
                "health checker config[enable] for service[%s/%s] is true, "
                "but config [chain] not found",
                service_key_.name_.c_str(), service_key_.namespace_.c_str());
    when_ = HealthCheckerConfig::kChainWhenNever;
    return kReturnOk;
  }

  Config* chain_config  = config->GetSubConfig("plugin");
  Config* plugin_config = NULL;
  Plugin* plugin        = NULL;
  HealthChecker* health_checker;
  ReturnCode ret;
  for (size_t i = 0; i < plugin_name_list.size(); i++) {
    std::string& plugin_name = plugin_name_list[i];
    ret = PluginManager::Instance().GetPlugin(plugin_name, kPluginHealthChecker, plugin);
    if (ret == kReturnOk) {
      health_checker = dynamic_cast<HealthChecker*>(plugin);
      if (health_checker == NULL) {
        continue;
      }
      plugin_config = chain_config->GetSubConfig(plugin_name);
      if (plugin_config == NULL) {
        continue;
      }

      ret = health_checker->Init(plugin_config, context);
      if (ret == kReturnOk) {
        POLARIS_LOG(LOG_INFO, "Init health checker plugin[%s] for service[%s/%s] success",
                    plugin_name.c_str(), service_key_.namespace_.c_str(),
                    service_key_.name_.c_str());
        health_checker_list_.push_back(health_checker);
      } else {
        POLARIS_LOG(LOG_ERROR, "Init health checker plugin[%s] for service[%s/%s] failed, skip it",
                    plugin_name.c_str(), service_key_.namespace_.c_str(),
                    service_key_.name_.c_str());
        delete health_checker;
        health_checker = NULL;
      }
      delete plugin_config;
      plugin_config = NULL;
    } else {
      POLARIS_LOG(LOG_ERROR,
                  "health checker plugin with name[%s] not found, skip it for service[%s/%s]",
                  plugin_name.c_str(), service_key_.namespace_.c_str(), service_key_.name_.c_str());
    }
  }
  delete chain_config;
  chain_config = NULL;

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
  POLARIS_LOG(LOG_INFO, "here detectInstance, namespace: %s, name: %s, when: %s",
              service_key_.namespace_.c_str(), service_key_.name_.c_str(), when_.c_str());
  uint64_t now_time_ms = Time::GetCurrentTimeMs();
  if (now_time_ms - last_detect_time_ms_ <= health_check_ttl_ms_) {
    return kReturnOk;
  }
  last_detect_time_ms_ = now_time_ms;

  if (local_registry_ == NULL) {
    POLARIS_LOG(LOG_ERROR, "The health checker local_registry_ of service[%s/%s] is null",
                service_key_.namespace_.c_str(), service_key_.name_.c_str());
    return kReturnOk;
  }

  ServiceData* service_data = NULL;
  std::vector<Instance*> health_check_instances;
  if (local_registry_->GetCircuitBreakerInstances(service_key_, service_data,
                                                  health_check_instances) != kReturnOk) {
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

  for (std::size_t i = 0; i < health_check_instances.size(); ++i) {
    bool is_detect_success = false;
    Instance* instance     = health_check_instances[i];
    for (std::size_t i = 0; i < health_checker_list_.size(); ++i) {
      HealthChecker*& detector = health_checker_list_[i];
      DetectResult detector_result;
      if (kReturnOk == detector->DetectInstance(*instance, detector_result)) {
        POLARIS_LOG(LOG_INFO,
                    "The detector[%s] of service[%s/%s] getting instance[%s-%s:%d] success[0], "
                    "elapsing %" PRIu64 " ms",
                    detector_result.detect_type.c_str(), service_key_.namespace_.c_str(),
                    service_key_.name_.c_str(), instance->GetId().c_str(),
                    instance->GetHost().c_str(), instance->GetPort(), detector_result.elapse);
        is_detect_success = true;
        break;
      } else {
        POLARIS_LOG(LOG_INFO,
                    "The detector[%s] of service[%s/%s] getting instance[%s-%s:%d] failed[%d],"
                    " elapsing %" PRIu64 " ms",
                    detector_result.detect_type.c_str(), service_key_.namespace_.c_str(),
                    service_key_.name_.c_str(), instance->GetId().c_str(),
                    instance->GetHost().c_str(), instance->GetPort(), detector_result.return_code,
                    detector_result.elapse);
      }
    }
    // 探活插件成功，则将熔断实例置为半开状态，其他实例状态不变
    // 探活插件失败，则将健康实例置为熔断状态，其他实例状态不变
    if (is_detect_success) {
      circuit_breaker_chain.TranslateStatus(instance->GetId(), kCircuitBreakerOpen,
                                            kCircuitBreakerHalfOpen);
      POLARIS_LOG(LOG_INFO,
                  "service[%s/%s] getting instance[%s-%s:%d] detectoring success, change to "
                  "half-open status",
                  service_key_.namespace_.c_str(), service_key_.name_.c_str(),
                  instance->GetId().c_str(), instance->GetHost().c_str(), instance->GetPort());
    } else {
      circuit_breaker_chain.TranslateStatus(instance->GetId(), kCircuitBreakerClose,
                                            kCircuitBreakerOpen);
      POLARIS_LOG(LOG_INFO,
                  "service[%s/%s] getting instance[%s-%s:%d] detectoring failed, change to "
                  "open status",
                  service_key_.namespace_.c_str(), service_key_.name_.c_str(),
                  instance->GetId().c_str(), instance->GetHost().c_str(), instance->GetPort());
    }
  }
  return kReturnOk;
}

std::vector<HealthChecker*> HealthCheckerChainImpl::GetHealthCheckers() {
  return health_checker_list_;
}

}  // namespace polaris
