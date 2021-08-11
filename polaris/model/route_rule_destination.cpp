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

#include "model/route_rule_destination.h"

#include "model/model_impl.h"

namespace polaris {

static const uint32_t kRuleDefaultWeight = 0;
static const bool kRuleDefaultIsolate    = false;

bool RouteRuleDestination::InitFromPb(const v1::Destination& destination) {
  service_key_.namespace_ = destination.namespace_().value();
  service_key_.name_      = destination.service().value();
  google::protobuf::Map<std::string, v1::MatchString>::const_iterator it;
  for (it = destination.metadata().begin(); it != destination.metadata().end(); ++it) {
    if (!metadata_[it->first].Init(it->second)) {
      return false;
    }
  }
  weight_  = destination.has_weight() ? destination.weight().value() : kRuleDefaultWeight;
  isolate_ = destination.has_isolate() ? destination.isolate().value() : kRuleDefaultIsolate;

  if (destination.has_transfer()) {
    transfer_service_ = destination.transfer().value();
  }
  return true;
}

bool RouteRuleDestination::FillSystemVariables(const SystemVariables& variables) {
  for (std::map<std::string, MatchString>::iterator it = metadata_.begin(); it != metadata_.end();
       ++it) {
    if (it->second.IsVariable()) {
      const std::string& variable_value = it->second.GetString();
      std::string value;
      if (!variable_value.empty() && variables.GetVariable(variable_value, value)) {
        if (!it->second.FillVariable(value)) {
          return false;
        }
      }
    }
  }
  return true;
}

bool RouteRuleDestination::MatchService(const ServiceKey& service_key) const {
  return (service_key_.namespace_ == service_key.namespace_ ||
          service_key_.namespace_ == MatchString::Wildcard()) &&
         (service_key_.name_ == service_key.name_ || service_key_.name_ == MatchString::Wildcard());
}

// 根据Destination计算计算实例分组
std::map<std::string, RuleRouterSet*> RouteRuleDestination::CalculateSet(
    const std::vector<Instance*>& instances, const std::set<Instance*>& unhealthy_set,
    const std::map<std::string, std::string>& parameters) const {
  //根据instance的元数据来区分set
  std::map<std::string, RuleRouterSet*> rule_router_set_map;
  for (std::vector<Instance*>::const_iterator instance_it = instances.begin();
       instance_it != instances.end(); ++instance_it) {
    if (MatchString::MapMatch(metadata_, (*instance_it)->GetMetadata(), parameters)) {
      RuleRouterSet* rule_router_set;
      SubSetInfo ss;
      //提取subset
      for (std::map<std::string, MatchString>::const_iterator it = metadata_.begin();
           it != metadata_.end(); ++it) {
        if (it->second.IsParameter()) {
          // 此处不判断find结果，已经匹配的情况下parameter必然存在key
          ss.subset_map_[it->first] = parameters.find(it->first)->second;
        } else {
          ss.subset_map_[it->first] = (*instance_it)->GetMetadata()[it->first];
        }
      }
      if (rule_router_set_map.find(ss.GetSubInfoStrId()) == rule_router_set_map.end()) {
        rule_router_set                           = new RuleRouterSet();
        rule_router_set->subset.subset_map_       = ss.subset_map_;
        rule_router_set->isolated_                = isolate_;
        rule_router_set_map[ss.GetSubInfoStrId()] = rule_router_set;
      } else {
        rule_router_set = rule_router_set_map[ss.GetSubInfoStrId()];
      }
      if (unhealthy_set.find(*instance_it) == unhealthy_set.end()) {
        rule_router_set->healthy_.push_back(*instance_it);
      } else {
        rule_router_set->unhealthy_.push_back(*instance_it);
      }
    }
  }
  return rule_router_set_map;
}

}  // namespace polaris
