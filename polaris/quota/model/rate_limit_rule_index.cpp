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

#include "quota/model/rate_limit_rule_index.h"

namespace polaris {

void RateLimitRuleSubIndex::AddRule(
    RateLimitRule* rule, const std::map<std::string, MatchString>::const_iterator& label_it,
    const std::map<std::string, MatchString>::const_iterator& label_end) {
  std::map<std::string, MatchString>::const_iterator next_label_it = label_it;
  const std::string& label_value                                   = label_it->second.GetString();
  while (++next_label_it != label_end) {
    if (next_label_it->second.IsExactText()) {
      sub_index_[label_value][next_label_it->first].AddRule(rule, next_label_it, label_end);
      return;
    }
  }
  value_index_[label_value] = rule;
}

RateLimitRule* RateLimitRuleSubIndex::Search(
    const std::string& value, const std::map<std::string, std::string>& subset,
    const std::map<std::string, std::string>& labels) const {
  // 先查找具有更多精确匹配label的索引
  std::map<std::string, std::map<std::string, RateLimitRuleSubIndex> >::const_iterator sub_it;
  std::map<std::string, RateLimitRuleSubIndex>::const_iterator key_it;
  if ((sub_it = sub_index_.find(value)) != sub_index_.end()) {
    for (key_it = sub_it->second.begin(); key_it != sub_it->second.end(); ++key_it) {
      std::map<std::string, std::string>::const_iterator label_it = labels.find(key_it->first);
      if (label_it != labels.end()) {
        RateLimitRule* rule = key_it->second.Search(label_it->second, subset, labels);
        if (rule != NULL) {
          return rule;
        }
      }
    }
  }
  // 在查找没有更多精确匹配的索引
  std::map<std::string, RateLimitRule*>::const_iterator value_it = value_index_.find(value);
  if (value_it != value_index_.end()) {
    if (value_it->second->IsMatch(subset, labels)) {
      return value_it->second;
    }
  }
  return NULL;
}

void RateLimitRuleIndex::AddRule(RateLimitRule* rule) {
  const std::map<std::string, MatchString>& labels      = rule->GetLables();
  std::map<std::string, MatchString>::const_iterator it = labels.begin();
  while (it != labels.end() && !it->second.IsExactText()) {
    it++;
  }
  if (it != labels.end()) {
    sub_index_[it->first].AddRule(rule, it, labels.end());
  } else {
    rules_.push_back(rule);
  }
}

RateLimitRule* RateLimitRuleIndex::MatchRule(
    const std::map<std::string, std::string>& subset,
    const std::map<std::string, std::string>& labels) const {
  // 优先匹配有精确label的规则
  std::map<std::string, RateLimitRuleSubIndex>::const_iterator sub_it;
  std::map<std::string, std::string>::const_iterator label_it;
  for (sub_it = sub_index_.begin(); sub_it != sub_index_.end(); ++sub_it) {
    label_it = labels.find(sub_it->first);
    if (label_it != labels.end()) {
      RateLimitRule* rule = sub_it->second.Search(label_it->second, subset, labels);
      if (rule != NULL) {
        return rule;
      }
    }
  }
  // 匹配没有精确label的规则
  for (std::size_t i = 0; i < rules_.size(); ++i) {
    RateLimitRule* const& rule = rules_[i];
    if (rule->IsMatch(subset, labels)) {
      return rule;
    }
  }
  return NULL;
}

}  // namespace polaris
