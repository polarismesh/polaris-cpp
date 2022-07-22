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

#ifndef POLARIS_CPP_POLARIS_MODEL_ROUTE_RULE_SOURCE_H_
#define POLARIS_CPP_POLARIS_MODEL_ROUTE_RULE_SOURCE_H_

#include <map>
#include <string>
#include <vector>

#include "context/system_variables.h"
#include "model/match_string.h"
#include "polaris/defs.h"
#include "polaris/noncopyable.h"
#include "v1/routing.pb.h"

namespace polaris {

// 请求来源匹配规则
class RouteRuleSource {
 public:
  bool InitFromPb(const v1::Source& source);

  bool FillSystemVariables(const SystemVariables& variables);

  bool Match(const ServiceKey& src_service, const ServiceKey& dst_service,
             const std::map<std::string, std::string>& metadata, std::string& parameter) const;

  bool IsWildcardRule() const;

 private:
  ServiceKey src_service_;
  ServiceKey dst_service_;
  std::map<std::string, MatchString> metadata_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_ROUTE_RULE_SOURCE_H_
