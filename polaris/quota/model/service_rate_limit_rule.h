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

#ifndef POLARIS_CPP_POLARIS_QUOTA_MODEL_SERVICE_RATE_LIMIT_RULE_H_
#define POLARIS_CPP_POLARIS_QUOTA_MODEL_SERVICE_RATE_LIMIT_RULE_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "quota/model/rate_limit_rule.h"
#include "quota/model/rate_limit_rule_index.h"

namespace polaris {

// 限流配额数据，由PB解析后存储在ServiceData对象中
class RateLimitData {
 public:
  ~RateLimitData();

  void AddRule(RateLimitRule* rule);

  void SortByPriority();  // 按优先级排序，相同优先级按ID排序

  void SetupIndexMap();  // 构建索引用于加快匹配

  RateLimitRule* MatchRule(const std::map<std::string, std::string>& subset,
                           const std::map<std::string, std::string>& labels) const;

  const std::set<std::string>& GetLabelKeys() const { return label_keys_; }

  const std::vector<RateLimitRule*>& GetRules() const { return rules_; }

 private:
  std::vector<RateLimitRule*> rules_;
  std::map<int, RateLimitRuleIndex> rule_index_;
  std::set<std::string> label_keys_;
};

class ServiceData;
// 服务限流配额数据，封装类型为限流的ServiceData
class ServiceRateLimitRule {
 public:
  explicit ServiceRateLimitRule(ServiceData* service_data);

  ~ServiceRateLimitRule();

  RateLimitRule* MatchRateLimitRule(const std::map<std::string, std::string>& subset,
                                    const std::map<std::string, std::string>& labels) const;

  // 检查规则是否还生效
  bool IsRuleEnable(RateLimitRule* rule);

  ServiceData* GetServiceDataWithRef();

  const std::set<std::string>& GetLabelKeys() const;

 private:
  ServiceData* service_data_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_MODEL_SERVICE_RATE_LIMIT_RULE_H_
