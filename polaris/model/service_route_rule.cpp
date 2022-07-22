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

#include "model/service_route_rule.h"

#include <pthread.h>

#include "model/model_impl.h"

namespace polaris {

ServiceRouteRule::ServiceRouteRule(ServiceData* service_data) : service_data_(service_data) {}

ServiceRouteRule::~ServiceRouteRule() {}

RouteRuleData* ServiceRouteRule::RouteRule() const { return service_data_->GetServiceDataImpl()->data_.route_rule_; }

const std::set<std::string>& ServiceRouteRule::GetKeys() const {
  return service_data_->GetServiceDataImpl()->data_.route_rule_->keys_;
}

ServiceData* ServiceRouteRule::GetServiceData() { return service_data_; }

// 根据路由的Source匹配，查找匹配的路由
bool ServiceRouteRule::RouteMatch(ServiceRouteRule* route_rule, const ServiceKey& dst_service,
                                  ServiceRouteRule* src_route_rule, ServiceInfo* source_service_info,
                                  RouteRuleBound*& matched_route, bool* match_outbounds, std::string& parameters) {
  // 优先匹配被调的入规则
  RouteRuleData* dst_rule_data = route_rule->RouteRule();
  std::vector<RouteRuleBound>& inbounds = dst_rule_data->inbounds_;
  for (std::size_t i = 0; i < inbounds.size(); ++i) {
    if (inbounds[i].route_rule_.MatchSource(source_service_info, dst_service, parameters)) {
      matched_route = &inbounds[i];
      *match_outbounds = false;
      return true;
    }
  }
  if (inbounds.size() > 0) {  // 被调服务有入规则，但却没有匹配到路由
    return false;
  }
  // 被调没有入规则，如果传入了主调服务信息，则再匹配主调的出规则
  if (src_route_rule != nullptr) {
    RouteRuleData* src_rule_data = src_route_rule->RouteRule();
    std::vector<RouteRuleBound>& outbounds = src_rule_data->outbounds_;
    for (std::size_t i = 0; i < outbounds.size(); ++i) {
      if (outbounds[i].route_rule_.MatchSource(source_service_info, dst_service, parameters)) {
        matched_route = &outbounds[i];
        *match_outbounds = true;
        return true;
      }
    }
    if (outbounds.size() > 0) {  // 主调服务有出规则，但却没有匹配到路由
      return false;
    }
  }
  // 被调无入规则，主调无出规则或者无需匹配
  return true;
}

InstancesSet* ServiceRouteRule::SelectSet(std::map<uint32_t, InstancesSet*>& cluster, uint32_t sum_weight) {
  static __thread bool thread_local_seed_not_init = true;
  static __thread unsigned int thread_local_seed = 0;
  if (thread_local_seed_not_init) {
    thread_local_seed_not_init = false;
    thread_local_seed = time(nullptr) ^ pthread_self();
  }
  uint32_t random_weight = rand_r(&thread_local_seed) % sum_weight;
  std::map<uint32_t, InstancesSet*>::iterator it = cluster.upper_bound(random_weight);
  it->second->GetImpl()->count_++;
  return it->second;
}

}  // namespace polaris
