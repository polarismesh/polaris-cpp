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

#ifndef POLARIS_CPP_POLARIS_MODEL_SERVICE_ROUTE_RULE_H_
#define POLARIS_CPP_POLARIS_MODEL_SERVICE_ROUTE_RULE_H_

#include <atomic>
#include <set>
#include <string>

#include "model/route_rule.h"
#include "polaris/model.h"

namespace polaris {

struct RouteRuleBound {
  RouteRuleBound() : recover_all_(false) {}
  explicit RouteRuleBound(polaris::RouteRuleBound&&) = delete;

  RouteRule route_rule_;
  std::atomic<bool> recover_all_;  // 是否全死全活
};

struct RouteRuleData {
  RouteRuleData(int inbound_size, int outbound_size) : inbounds_(inbound_size), outbounds_(outbound_size) {}

  std::vector<RouteRuleBound> inbounds_;
  std::vector<RouteRuleBound> outbounds_;
  std::set<std::string> keys_;  // 规则中设置的key
};

/// @brief 服务路由：封装类型为服务路由的服务数据提供服务路由接口
class ServiceRouteRule : Noncopyable {
 public:
  explicit ServiceRouteRule(ServiceData* data);

  ~ServiceRouteRule();

  RouteRuleData* RouteRule() const;

  const std::set<std::string>& GetKeys() const;

  /// @brief 获取封装的服务数据
  ServiceData* GetServiceData();

  static bool RouteMatch(ServiceRouteRule* route_rule, const ServiceKey& dst_service, ServiceRouteRule* src_route_rule,
                         ServiceInfo* source_service_info, RouteRuleBound*& matched_route, bool* match_outbounds,
                         std::string& parameters);

  static InstancesSet* SelectSet(std::map<uint32_t, InstancesSet*>& cluster, uint32_t sum_weight);

 private:
  ServiceData* service_data_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_SERVICE_ROUTE_RULE_H_
