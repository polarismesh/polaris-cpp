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

#ifndef POLARIS_CPP_POLARIS_MODEL_ROUTE_RULE_H_
#define POLARIS_CPP_POLARIS_MODEL_ROUTE_RULE_H_

#include <map>
#include <string>
#include <vector>

#include "model/route_rule_destination.h"
#include "model/route_rule_source.h"
#include "polaris/model.h"

namespace polaris {

// 服务配置的一条路由规则
class RouteRule {
public:
  RouteRule() : is_valid_(true) {}

  bool InitFromPb(const v1::Route& route);

  void FillSystemVariables(const SystemVariables& variables);

  // 检查源是否匹配
  bool MatchSource(ServiceInfo* serice_info, std::string& parameters) const;

  bool CalculateSet(ServiceKey& service_key, bool match_service,
                    const std::vector<Instance*>& instances,
                    const std::set<Instance*>& unhealthy_set,
                    const std::map<std::string, std::string>& parameters,
                    std::map<uint32_t, std::vector<RuleRouterSet*> >& result) const;

private:
  bool is_valid_;
  // 多条源匹配规则，只有一个源匹配则这个规则就匹配
  std::vector<RouteRuleSource> sources_;

  // 多种优先级的目标匹配规则
  // sort by priority
  std::map<uint32_t, std::vector<RouteRuleDestination> > destinations_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_ROUTE_RULE_H_
