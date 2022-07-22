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

#ifndef POLARIS_CPP_POLARIS_MODEL_ROUTE_RULE_DESTINATION_H_
#define POLARIS_CPP_POLARIS_MODEL_ROUTE_RULE_DESTINATION_H_

#include <map>
#include <string>
#include <vector>

#include "model/match_string.h"
#include "model/model_impl.h"
#include "polaris/noncopyable.h"
#include "v1/routing.pb.h"

namespace polaris {

// 实例分组数据
struct RuleRouterSet {
  uint32_t weight_;
  std::vector<Instance*> healthy_;
  std::vector<Instance*> unhealthy_;
  SubSetInfo subset;  // subset
  bool isolated_;     //是否被隔离
};

// 请求目标实例匹配规则
class RouteRuleDestination {
 public:
  RouteRuleDestination() : weight_(0), isolate_(false) {}

  bool InitFromPb(const v1::Destination& destination);

  bool FillSystemVariables(const SystemVariables& variables);

  bool MatchService(const ServiceKey& service_key) const;

  std::map<std::string, RuleRouterSet*> CalculateSet(const std::vector<Instance*>& instances,
                                                     const std::set<Instance*>& unhealthy_set,
                                                     const std::map<std::string, std::string>& parameters) const;

  bool HasTransfer() const { return !transfer_service_.empty(); }

  const std::map<std::string, MatchString>& GetMetaData() const { return metadata_; }

  const std::string& TransferService() const { return transfer_service_; }

  uint32_t GetWeight() const { return weight_; }

  bool IsIsolate() const { return isolate_; }

 private:
  ServiceKey service_key_;
  std::map<std::string, MatchString> metadata_;

  uint32_t weight_;  // 权重
  bool isolate_;     // 是否隔离

  std::string transfer_service_;  // 转发到另一个服务下实例
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_ROUTE_RULE_DESTINATION_H_
