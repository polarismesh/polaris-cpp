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

#include "model/route_rule.h"

#include "model/model_impl.h"

namespace polaris {

static const uint32_t kRuleDefaultPriority = 9;

bool RouteRule::InitFromPb(const v1::Route& route) {
  for (int i = 0; i < route.sources_size(); ++i) {
    const v1::Source& pb_source = route.sources(i);
    RouteRuleSource source;
    is_valid_ = is_valid_ && source.InitFromPb(pb_source);
    sources_.push_back(source);  // 初始化失败也加入礼拜，但不会被匹配
  }
  for (int i = 0; i < route.destinations_size(); ++i) {
    const v1::Destination& pb_dest = route.destinations(i);
    uint32_t priority = pb_dest.has_priority() ? pb_dest.priority().value() : kRuleDefaultPriority;
    RouteRuleDestination destination;
    is_valid_ = is_valid_ && destination.InitFromPb(pb_dest);
    destinations_[priority].push_back(destination);
  }
  return is_valid_;
}

void RouteRule::FillSystemVariables(const SystemVariables& variables) {
  for (std::size_t i = 0; i < sources_.size(); i++) {
    is_valid_ = is_valid_ && sources_[i].FillSystemVariables(variables);
  }
  std::map<uint32_t, std::vector<RouteRuleDestination> >::iterator dest_it;
  for (dest_it = destinations_.begin(); dest_it != destinations_.end(); ++dest_it) {
    std::vector<RouteRuleDestination>& dest_list = dest_it->second;
    for (std::size_t i = 0; i < dest_list.size(); i++) {
      is_valid_ = is_valid_ && dest_list[i].FillSystemVariables(variables);
    }
  }
}

bool RouteRule::MatchSource(ServiceInfo* service_info, std::string& parameters) const {
  if (!is_valid_) {
    return false;
  }
  if (service_info != NULL) {
    for (std::size_t i = 0; i < sources_.size(); ++i) {
      if (sources_[i].Match(service_info->service_key_, service_info->metadata_, parameters)) {
        return true;
      }
    }
  } else {
    for (std::size_t i = 0; i < sources_.size(); ++i) {
      if (sources_[i].IsWildcardRule()) {
        return true;
      }
    }
  }
  return sources_.empty();
}

// 根据Destination计算路由结果
bool RouteRule::CalculateSet(ServiceKey& service_key, bool match_service,
                             const std::vector<Instance*>& instances,
                             const std::set<Instance*>& unhealthy_set,
                             const std::map<std::string, std::string>& parameters,
                             std::map<uint32_t, std::vector<RuleRouterSet*> >& result) const {
  for (std::map<uint32_t, std::vector<RouteRuleDestination> >::const_iterator it =
           destinations_.begin();
       it != destinations_.end(); ++it) {
    for (std::size_t i = 0; i < it->second.size(); ++i) {
      const RouteRuleDestination& dest = it->second[i];
      // 主调的outbound要匹配被调服务，被调的inbound是自己跳过检查
      if (match_service && !dest.MatchService(service_key)) {
        continue;
      }
      if (dest.HasTransfer()) {  // 需要转发
        service_key.name_ = dest.TransferService();
        return false;
      }
      // 计算实例分组
      //根据instance的元数据来区分set
      std::map<std::string, RuleRouterSet*> rule_router_set_map =
          dest.CalculateSet(instances, unhealthy_set, parameters);
      for (std::map<std::string, RuleRouterSet*>::iterator map_it = rule_router_set_map.begin();
           map_it != rule_router_set_map.end(); ++map_it) {
        RuleRouterSet* rule_router_set = map_it->second;
        if (!rule_router_set->healthy_.empty() || !rule_router_set->unhealthy_.empty()) {
          rule_router_set->weight_ = dest.GetWeight();
          result[it->first].push_back(rule_router_set);
        } else {
          delete rule_router_set;
        }
      }
    }
  }
  return true;
}

}  // namespace polaris
