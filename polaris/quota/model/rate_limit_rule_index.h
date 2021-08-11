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

#ifndef POLARIS_CPP_POLARIS_QUOTA_MODEL_RATE_LIMIT_RULE_INDEX_H_
#define POLARIS_CPP_POLARIS_QUOTA_MODEL_RATE_LIMIT_RULE_INDEX_H_

#include <map>
#include <string>
#include <vector>

#include "quota/model/rate_limit_rule.h"

namespace polaris {

// 针对label的key和value构建的索引
class RateLimitRuleSubIndex {
public:
  // 插入规则
  void AddRule(RateLimitRule* rule,
               const std::map<std::string, MatchString>::const_iterator& label_it,
               const std::map<std::string, MatchString>::const_iterator& label_end);

  // 用当前索引查询rule
  RateLimitRule* Search(const std::string& value, const std::map<std::string, std::string>& subset,
                        const std::map<std::string, std::string>& labels) const;

private:
  // value -> rule 最后一个label的value的索引
  std::map<std::string, RateLimitRule*> value_index_;

  // value -> <key, sub_index>  非最后一个label的value，指向下一个label的key
  std::map<std::string, std::map<std::string, RateLimitRuleSubIndex> > sub_index_;
};

// 限流规则索引
class RateLimitRuleIndex {
public:
  // 插入规则索引
  void AddRule(RateLimitRule* rule);

  // 查找rule
  RateLimitRule* MatchRule(const std::map<std::string, std::string>& subset,
                           const std::map<std::string, std::string>& labels) const;

private:
  std::vector<RateLimitRule*> rules_;  // 没有精确匹配label

  // <key, index> 第一个精确匹配的label的key的索引
  std::map<std::string, RateLimitRuleSubIndex> sub_index_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_MODEL_RATE_LIMIT_RULE_INDEX_H_
