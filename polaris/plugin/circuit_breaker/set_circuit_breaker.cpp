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

#include "plugin/circuit_breaker/set_circuit_breaker.h"

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/wrappers.pb.h>
#include <stddef.h>
#include <v1/circuitbreaker.pb.h>
#include <v1/model.pb.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "context_internal.h"
#include "logger.h"
#include "model/match_string.h"
#include "model/model_impl.h"
#include "plugin/circuit_breaker/metric_window_manager.h"
#include "plugin/circuit_breaker/set_circuit_breaker_chain_data.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "polaris/log.h"
#include "polaris/model.h"

namespace polaris {

class CircuitBreakerExecutor;

static bool MetadataMatch(const google::protobuf::Map<std::string, v1::MatchString>& rule_metadata,
                          const std::map<std::string, std::string>& metadata) {
  if (rule_metadata.size() < metadata.size()) {
    return false;
  }
  google::protobuf::Map<std::string, v1::MatchString>::const_iterator rule_it;
  for (rule_it = rule_metadata.begin(); rule_it != rule_metadata.end(); ++rule_it) {
    std::map<std::string, std::string>::const_iterator it = metadata.find(rule_it->first);
    if (it == metadata.end()) {
      return false;
    }
    MatchString match_string;
    if (!match_string.Init(rule_it->second) || !match_string.Match(it->second)) {
      return false;
    }
  }
  return true;
}

static bool NameAndServiceMatch(const std::string& rule_namespace,
                                const std::string& rule_service_name,
                                const std::string& namespace_str, const std::string& service_name) {
  if ((rule_namespace == namespace_str || rule_namespace == "*") &&
      (rule_service_name == service_name || rule_service_name == "*")) {
    return true;
  }
  return false;
}

std::string ConvertMapToStr(std::map<std::string, std::string>& m) {
  std::vector<std::string> tmp_vec;
  std::map<std::string, std::string>::iterator iter;
  for (iter = m.begin(); iter != m.end(); ++iter) {
    std::string tmp_str = iter->first + ":" + iter->second;
    tmp_vec.push_back(tmp_str);
  }
  std::sort(tmp_vec.begin(), tmp_vec.end());
  std::string ret_str = "";
  for (size_t i = 0; i < tmp_vec.size(); ++i) {
    ret_str += tmp_vec[i];
    ret_str += "|";
  }
  ret_str = ret_str.substr(0, ret_str.size() - 1);
  return ret_str;
}

std::string SubSetInfo::GetSubInfoStrId() {
  if (this->subset_info_str.empty()) {
    if (this->subset_map_.empty()) {
      return "";
    }
    this->subset_info_str = ConvertMapToStr(this->subset_map_);
    return this->subset_info_str;
  } else {
    return this->subset_info_str;
  }
}

std::string Labels::GetLabelStr() {
  if (this->labels_str.empty()) {
    if (this->labels_.empty()) {
      return "";
    }
    this->labels_str = ConvertMapToStr(this->labels_);
    return this->labels_str;
  } else {
    return this->labels_str;
  }
}

SetCircuitBreakerImpl::SetCircuitBreakerImpl(const ServiceKey& service_key)
    : service_key_(service_key) {
  windows_manager_ = NULL;
  context_         = NULL;
  chain_data_impl_ = NULL;
  enable_          = false;
}

SetCircuitBreakerImpl::~SetCircuitBreakerImpl() {
  context_ = NULL;
  if (chain_data_impl_ != NULL) {
    chain_data_impl_->MarkDeleted();
    chain_data_impl_->DecrementRef();
  }
  if (windows_manager_ != NULL) {
    delete windows_manager_;
    windows_manager_ = NULL;
  }
}

ReturnCode SetCircuitBreakerImpl::Init(Config* plugin_config, Context* context) {
  context_ = context;
  enable_  = plugin_config->GetBoolOrDefault("enable", false);
  if (!enable_) {
    return kReturnNotInit;
  }

  ContextImpl* context_impl                        = context->GetContextImpl();
  CircuitBreakerExecutor* circuit_breaker_executor = context_impl->GetCircuitBreakerExecutor();
  LocalRegistry* local_registry                    = context_->GetLocalRegistry();
  windows_manager_ = new MetricWindowManager(context_, circuit_breaker_executor);
  chain_data_impl_ = new CircuitBreakSetChainData(service_key_, local_registry, windows_manager_,
                                                  context->GetContextImpl()->GetServiceRecord());
  return kReturnOk;
}

ReturnCode SetCircuitBreakerImpl::GetCbPConfPbFromLocalRegistry(ServiceData*& service_data,
                                                                v1::CircuitBreaker*& pb_conf) {
  LocalRegistry* local_registry = context_->GetLocalRegistry();
  ReturnCode ret_code =
      local_registry->GetServiceDataWithRef(service_key_, kCircuitBreakerConfig, service_data);
  if (ret_code != kReturnOk) {
    ServiceDataNotify* conf_notify_tmp = NULL;
    local_registry->LoadServiceDataWithNotify(service_key_, kCircuitBreakerConfig, service_data,
                                              conf_notify_tmp);
  }
  if (service_data == NULL) {
    return kReturnNotInit;
  }
  if (service_data->GetServiceDataImpl()->GetCircuitBreaker() == NULL) {
    service_data->DecrementRef();
    return kReturnNotInit;
  }
  pb_conf = &(*service_data->GetServiceDataImpl()->GetCircuitBreaker());
  return kReturnOk;
}

ReturnCode SetCircuitBreakerImpl::MatchDestinationSet(v1::CircuitBreaker* pb_conf,
                                                      const InstanceGauge& gauge,
                                                      v1::DestinationSet*& dst_conf_ptr) {
  for (int i = 0; i < pb_conf->inbounds_size(); ++i) {
    const v1::CbRule cb_rule = pb_conf->inbounds(i);
    bool match               = false;
    // 匹配sourceMatchers
    for (int j = 0; j < cb_rule.sources_size(); ++j) {
      const v1::SourceMatcher& source = cb_rule.sources(j);
      if (NameAndServiceMatch(source.namespace_().value(), source.service().value(),
                              gauge.source_service_key.namespace_,
                              gauge.source_service_key.name_)) {
        if (source.labels().empty()) {
          if (gauge.labels_.empty()) {
            POLARIS_LOG(POLARIS_TRACE, "[SET-CIRCUIT-BREAKER]CbRuleCircuitBreaker::Match");
            match = true;
            break;
          }
        } else {
          if (!gauge.labels_.empty()) {
            if (MetadataMatch(source.labels(), gauge.labels_)) {
              POLARIS_LOG(POLARIS_TRACE, "[SET-CIRCUIT-BREAKER]CbRuleCircuitBreaker::Match");
              match = true;
              break;
            }
          }
        }
      }
    }
    if (!match) {
      continue;
    }
    // 匹配DestinationSet
    match = false;
    for (int j = 0; j < cb_rule.destinations_size(); ++j) {
      const v1::DestinationSet& dst_conf = cb_rule.destinations(j);
      if (NameAndServiceMatch(dst_conf.namespace_().value(), dst_conf.service().value(),
                              gauge.service_namespace, gauge.service_name)) {
        if ((dst_conf.metadata().empty() && gauge.subset_.empty()) ||
            MetadataMatch(dst_conf.metadata(), gauge.subset_)) {
          dst_conf_ptr = pb_conf->mutable_inbounds(i)->mutable_destinations(j);
          return kReturnOk;
        }
      }
    }
  }
  return kReturnInvalidConfig;
}

ReturnCode SetCircuitBreakerImpl::RealTimeCircuitBreak(const InstanceGauge& instance_gauge) {
  if (instance_gauge.subset_.empty() && instance_gauge.labels_.empty()) {
    return kReturnOk;
  }
  ServiceData* service_data;
  v1::CircuitBreaker* pb_conf = NULL;
  ReturnCode ret_code         = GetCbPConfPbFromLocalRegistry(service_data, pb_conf);
  if (ret_code != kReturnOk) {
    return kReturnOk;
  }

  if (pb_conf == NULL) {
    service_data->DecrementRef();
    return kReturnOk;
  }
  SubSetInfo subset_info;
  subset_info.subset_map_ = instance_gauge.subset_;
  Labels labels;
  labels.labels_       = instance_gauge.labels_;
  MetricWindow* window = windows_manager_->GetWindow(subset_info, labels);
  if (window == NULL || window->GetVersion() != pb_conf->revision().value()) {
    if (window != NULL) {
      window->DecrementRef();
    }
    v1::DestinationSet* dst_conf_ptr;
    ret_code = MatchDestinationSet(pb_conf, instance_gauge, dst_conf_ptr);
    if (ret_code != kReturnOk) {
      service_data->DecrementRef();
      POLARIS_LOG(POLARIS_DEBUG, "[SET-CIRCUIT-BREAKER] not match");
      return kReturnOk;
    }
    window = windows_manager_->UpdateWindow(service_key_, subset_info, labels,
                                            pb_conf->revision().value(), dst_conf_ptr,
                                            pb_conf->id().value(), chain_data_impl_);
  }
  ret_code = window->AddCount(instance_gauge);
  service_data->DecrementRef();
  window->DecrementRef();
  return ret_code;
}

ReturnCode SetCircuitBreakerImpl::TimingCircuitBreak() {
  ReturnCode return_code = chain_data_impl_->CheckAndSyncToRegistry();
  if (return_code != kReturnOk) {
    POLARIS_LOG(POLARIS_ERROR, "set circuit breaker check and sync to registry error:%d",
                return_code);
  }
  windows_manager_->WindowGc();
  return return_code;
}

}  // namespace polaris