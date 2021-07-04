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

#include "plugin/outlier_detector/outlier_detector.h"

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

OutlierDetectorChainImpl::OutlierDetectorChainImpl(const ServiceKey& service_key,
                                                   LocalRegistry* local_registry,
                                                   CircuitBreakerChain* circuit_breaker_chain) {
  service_key_           = service_key;
  local_registry_        = local_registry;
  circuit_breaker_chain_ = circuit_breaker_chain;
  enable_                = false;
  detector_ttl_ms_       = 0;
  last_detect_time_ms_   = Time::GetCurrentTimeMs();
}

OutlierDetectorChainImpl::~OutlierDetectorChainImpl() {
  for (std::size_t i = 0; i < outlier_detector_list_.size(); ++i) {
    delete outlier_detector_list_[i];
  }
  outlier_detector_list_.clear();
  local_registry_        = NULL;
  circuit_breaker_chain_ = NULL;
}

ReturnCode OutlierDetectorChainImpl::Init(Config* config, Context* context) {
  enable_ = config->GetBoolOrDefault(OutlierDetectorConfig::kChainEnableKey,
                                     OutlierDetectorConfig::kChainEnableDefault);
  if (enable_ == false) {
    return kReturnOk;
  }
  POLARIS_LOG(LOG_INFO, "outlier detector for service[%s/%s] is enable",
              service_key_.namespace_.c_str(), service_key_.name_.c_str());

  detector_ttl_ms_ = config->GetMsOrDefault(OutlierDetectorConfig::kDetectorIntervalKey,
                                            OutlierDetectorConfig::kDetectorIntervalDefault);

  std::vector<std::string> plugin_name_list = config->GetListOrDefault(
      OutlierDetectorConfig::kChainPluginListKey, OutlierDetectorConfig::kChainPluginListDefault);
  if (plugin_name_list.empty()) {
    POLARIS_LOG(LOG_WARN,
                "outlier detector config[enable] for service[%s/%s] is true, "
                "but config [chain] not found",
                service_key_.name_.c_str(), service_key_.namespace_.c_str());
    enable_ = false;
    return kReturnOk;
  }

  Config* chain_config  = config->GetSubConfig("plugin");
  Config* plugin_config = NULL;
  Plugin* plugin        = NULL;
  OutlierDetector* outlier_detector;
  ReturnCode ret;
  for (size_t i = 0; i < plugin_name_list.size(); i++) {
    std::string& plugin_name = plugin_name_list[i];
    ret = PluginManager::Instance().GetPlugin(plugin_name, kPluginOutlierDetector, plugin);
    if (ret == kReturnOk) {
      outlier_detector = dynamic_cast<OutlierDetector*>(plugin);
      if (outlier_detector == NULL) {
        continue;
      }
      plugin_config = chain_config->GetSubConfig(plugin_name);
      if (plugin_config == NULL) {
        continue;
      }

      ret = outlier_detector->Init(plugin_config, context);
      if (ret == kReturnOk) {
        POLARIS_LOG(LOG_INFO, "Init outlier detector plugin[%s] for service[%s/%s] success",
                    plugin_name.c_str(), service_key_.namespace_.c_str(),
                    service_key_.name_.c_str());
        outlier_detector_list_.push_back(outlier_detector);
      } else {
        POLARIS_LOG(
            LOG_ERROR, "Init outlier detector plugin[%s] for service[%s/%s] failed, skip it",
            plugin_name.c_str(), service_key_.namespace_.c_str(), service_key_.name_.c_str());
        delete outlier_detector;
        outlier_detector = NULL;
      }
      delete plugin_config;
      plugin_config = NULL;
    } else {
      POLARIS_LOG(LOG_ERROR,
                  "Outlier detector plugin with name[%s] not found, skip it for service[%s/%s]",
                  plugin_name.c_str(), service_key_.namespace_.c_str(), service_key_.name_.c_str());
    }
  }
  delete chain_config;
  chain_config = NULL;

  if (outlier_detector_list_.empty()) {
    POLARIS_LOG(LOG_ERROR,
                "The outlier detector of service[%s/%s] lost because outlier detector chain"
                " init failed",
                service_key_.namespace_.c_str(), service_key_.name_.c_str());
    enable_ = false;
  }
  return kReturnOk;
}

ReturnCode OutlierDetectorChainImpl::DetectInstance() {
  uint64_t now_time_ms = Time::GetCurrentTimeMs();
  if (now_time_ms - last_detect_time_ms_ <= detector_ttl_ms_) {
    return kReturnOk;
  }
  last_detect_time_ms_ = now_time_ms;

  if (local_registry_ == NULL) {
    POLARIS_LOG(LOG_ERROR, "The outlier detector local_registry_ of service[%s/%s] is null",
                service_key_.namespace_.c_str(), service_key_.name_.c_str());
    return kReturnOk;
  }

  ServiceData* service_data = NULL;
  local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  if (service_data == NULL) {
    return kReturnOk;
  }
  Service* service = service_data->GetService();
  ServiceInstances service_instances(service_data);
  std::map<std::string, Instance*>& instance_map      = service_instances.GetInstances();
  std::set<std::string> circuit_breaker_open_instance = service->GetCircuitBreakerOpenInstances();
  for (std::set<std::string>::iterator it = circuit_breaker_open_instance.begin();
       it != circuit_breaker_open_instance.end(); ++it) {
    const std::string& instance_id                  = *it;
    std::map<std::string, Instance*>::iterator iter = instance_map.find(instance_id);
    if (iter == instance_map.end()) {
      POLARIS_LOG(LOG_INFO, "The outlier detector of service[%s/%s] getting instance[%s] failed",
                  service_key_.namespace_.c_str(), service_key_.name_.c_str(), instance_id.c_str());
      continue;
    }
    bool half_open_flag = false;
    Instance* instance  = iter->second;
    for (std::size_t i = 0; i < outlier_detector_list_.size(); ++i) {
      OutlierDetector*& detector = outlier_detector_list_[i];
      DetectResult detector_result;
      if (kReturnOk == detector->DetectInstance(*instance, detector_result)) {
        POLARIS_LOG(LOG_INFO,
                    "The detector[%s] of service[%s/%s] getting instance[%s-%s:%d] success[0], "
                    "elapsing %" PRIu64 " ms",
                    detector_result.detect_type.c_str(), service_key_.namespace_.c_str(),
                    service_key_.name_.c_str(), instance->GetId().c_str(),
                    instance->GetHost().c_str(), instance->GetPort(), detector_result.elapse);
        half_open_flag = true;
        break;
      } else {
        POLARIS_LOG(LOG_INFO,
                    "The detector[%s] of service[%s/%s] getting instance[%s-%s:%d] success[%d],"
                    " elapsing %" PRIu64 " ms",
                    detector_result.detect_type.c_str(), service_key_.namespace_.c_str(),
                    service_key_.name_.c_str(), instance->GetId().c_str(),
                    instance->GetHost().c_str(), instance->GetPort(), detector_result.return_code,
                    detector_result.elapse);
      }
    }
    // 探活插件成功，则将该实例置为半开状态
    if (half_open_flag) {
      circuit_breaker_chain_->TranslateStatus(instance_id, kCircuitBreakerOpen,
                                              kCircuitBreakerHalfOpen);
      POLARIS_LOG(LOG_INFO,
                  "service[%s/%s] getting instance[%s-%s:%d] detectoring success, change to "
                  "half-open status",
                  service_key_.namespace_.c_str(), service_key_.name_.c_str(),
                  instance->GetId().c_str(), instance->GetHost().c_str(), instance->GetPort());
    }
  }
  return kReturnOk;
}

std::vector<OutlierDetector*> OutlierDetectorChainImpl::GetOutlierDetectors() {
  return outlier_detector_list_;
}

}  // namespace polaris
