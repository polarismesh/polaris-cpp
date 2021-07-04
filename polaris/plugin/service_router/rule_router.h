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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_RULE_ROUTER_H_
#define POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_RULE_ROUTER_H_

#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "cache/service_cache.h"
#include "model/match_string.h"
#include "model/model_impl.h"
#include "polaris/defs.h"
#include "polaris/plugin.h"

namespace polaris {

// 实例分组
class RuleRouterCluster {
public:
  ~RuleRouterCluster();

  bool CalculateByRoute(const RouteRule& route, ServiceKey& service_key, bool match_service,
                        const std::vector<Instance*>& instances,
                        const std::set<Instance*>& unhealthy_set,
                        const std::map<std::string, std::string>& parameters);

  bool CalculateRouteResult(std::vector<RuleRouterSet*>& result, uint32_t* sum_weight,
                            float percent_of_min_instances, bool enable_recover_all);

  void CalculateSubset(ServiceInstances* service_instances, Labels& labels);

  bool GetHealthyAndHalfOpenSubSet(
      std::vector<RuleRouterSet*>& cluster,
      std::map<std::string, SetCircuitBreakerUnhealthyInfo>& circuit_breaker_sets,
      std::vector<RuleRouterSet*>& cluster_halfopen, Labels& labels);

  bool GetHealthySubSet(std::vector<RuleRouterSet*>& cluster,
                        std::map<std::string, SetCircuitBreakerUnhealthyInfo>& circuit_breaker_sets,
                        Labels& labels);

  RuleRouterSet* GetDownGradeSubset(
      std::map<std::string, SetCircuitBreakerUnhealthyInfo>& circuit_breaker_sets, Labels& labels);

  void GetSetBreakerInfo(
      std::map<std::string, SetCircuitBreakerUnhealthyInfo>& circuit_breaker_sets,
      SubSetInfo& subset, Labels& labels, SetCircuitBreakerUnhealthyInfo** breaker_info);

public:
  std::map<uint32_t, std::vector<RuleRouterSet*> > data_;
};

// 规则路由实现
class RuleServiceRouter : public ServiceRouter {
public:
  RuleServiceRouter();

  virtual ~RuleServiceRouter();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual ReturnCode DoRoute(RouteInfo& route_info, RouteResult* route_result);

  virtual RouterStatData* CollectStat();

  static InstancesSet* SelectSet(std::map<uint32_t, InstancesSet*>& cluster, uint32_t sum_weight);

  static bool RouteMatch(ServiceRouteRule* route_rule, ServiceRouteRule* src_route_rule,
                         ServiceInfo* source_service_info, RouteRuleBound*& matched_route,
                         bool* match_outbounds, bool* have_route_rule, std::string& parameters);

private:
  bool enable_recover_all_;
  float percent_of_min_instances_;
  Context* context_;
  ServiceCache<RuleRouteCacheKey>* router_cache_;
  sync::Atomic<int> not_match_count_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_RULE_ROUTER_H_
