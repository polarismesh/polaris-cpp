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

#include "quota/model/service_rate_limit_rule.h"

#include <algorithm>

#include "model/model_impl.h"
#include "polaris/model.h"

namespace polaris {

RateLimitData::~RateLimitData() {
  for (std::size_t i = 0; i < rules_.size(); ++i) {
    delete rules_[i];
  }
}

void RateLimitData::AddRule(RateLimitRule* rule) {
  rules_.push_back(rule);
  if (!rule->IsDisbale()) {
    const std::map<std::string, MatchString>& labels = rule->GetLables();
    for (std::map<std::string, MatchString>::const_iterator it = labels.begin(); it != labels.end();
         ++it) {
      label_keys_.insert(it->first);
    }
  }
}

// 用于按优先级排序
bool RateLimitRulePtrCompare(RateLimitRule* lhs, RateLimitRule* rhs) {
  if (lhs->GetPriority() < rhs->GetPriority()) {
    return true;
  } else if (lhs->GetPriority() == rhs->GetPriority()) {
    return lhs->GetId() < rhs->GetId();
  } else {
    return false;
  }
}

void RateLimitData::SortByPriority() {
  std::sort(rules_.begin(), rules_.end(), RateLimitRulePtrCompare);
}

void RateLimitData::SetupIndexMap() {
  for (std::size_t i = 0; i < rules_.size(); ++i) {
    RateLimitRule* rule = rules_[i];
    rule_index_[rule->GetPriority()].AddRule(rule);
  }
}

RateLimitRule* RateLimitData::MatchRule(const std::map<std::string, std::string>& subset,
                                        const std::map<std::string, std::string>& labels) const {
  if (rule_index_.empty()) {  // 没有索引时，线性查找
    for (std::size_t i = 0; i < rules_.size(); ++i) {
      RateLimitRule* const& rule = rules_[i];
      if (rule->IsMatch(subset, labels)) {
        return rule;
      }
    }
    return NULL;
  }
  // 使用索引查询
  for (std::map<int, RateLimitRuleIndex>::const_iterator it = rule_index_.begin();
       it != rule_index_.end(); ++it) {
    RateLimitRule* rule = it->second.MatchRule(subset, labels);
    if (rule != NULL) {
      return rule;
    }
  }
  return NULL;
}

ServiceRateLimitRule::ServiceRateLimitRule(ServiceData* service_data)
    : service_data_(service_data) {}

ServiceRateLimitRule::~ServiceRateLimitRule() {
  if (service_data_ != NULL) {
    service_data_->DecrementRef();
    service_data_ = NULL;
  }
}

RateLimitRule* ServiceRateLimitRule::MatchRateLimitRule(
    const std::map<std::string, std::string>& subset,
    const std::map<std::string, std::string>& labels) const {
  RateLimitData* rate_limit_data = service_data_->GetServiceDataImpl()->GetRateLimitData();
  return rate_limit_data->MatchRule(subset, labels);
}

const std::set<std::string>& ServiceRateLimitRule::GetLabelKeys() const {
  RateLimitData* rate_limit_data = service_data_->GetServiceDataImpl()->GetRateLimitData();
  return rate_limit_data->GetLabelKeys();
}

ServiceData* ServiceRateLimitRule::GetServiceDataWithRef() {
  service_data_->IncrementRef();
  return service_data_;
}

}  // namespace polaris
