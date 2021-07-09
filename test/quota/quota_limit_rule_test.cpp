//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use
//  this file
//  except in compliance with the License. You may obtain a copy of the License
//  at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the
//  specific
//  language governing permissions and limitations under the License.
//

#include "quota/model/service_rate_limit_rule.h"

#include <gtest/gtest.h>

#include "utils/string_utils.h"

namespace polaris {

void InitRuleAmount(v1::Rule& rule) {
  for (int i = 1; i <= 5; i++) {
    v1::Amount* amount = rule.add_amounts();
    amount->mutable_maxamount()->set_value(10 * i);
    amount->mutable_validduration()->set_seconds(i);
  }
}

TEST(RateLimitRuleTest, RuleDisable) {
  RateLimitRule rate_limit_rule;
  v1::Rule rule;
  rule.mutable_disable()->set_value(true);
  ASSERT_FALSE(rate_limit_rule.Init(rule));
}

TEST(RateLimitRuleTest, RegexMatchString) {
  RateLimitRule rate_limit_rule;
  v1::Rule rule;
  InitRuleAmount(rule);
  ASSERT_TRUE(rate_limit_rule.Init(rule));

  v1::MatchString match_string;
  match_string.set_type(v1::MatchString::REGEX);
  match_string.mutable_value()->set_value("\\");
  (*rule.mutable_labels())["key"] = match_string;
  ASSERT_FALSE(rate_limit_rule.Init(rule));

  match_string.mutable_value()->set_value("regex.*");
  (*rule.mutable_labels())["key"] = match_string;
  ASSERT_TRUE(rate_limit_rule.Init(rule));

  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  labels["key"] = "re111";
  ASSERT_FALSE(rate_limit_rule.IsMatch(subset, labels));
  labels["key"] = "regex111";
  ASSERT_TRUE(rate_limit_rule.IsMatch(subset, labels));
}

TEST(RateLimitRuleTest, MatchWithEmptyLabels) {
  RateLimitRule rate_limit_rule;
  v1::Rule rule;
  InitRuleAmount(rule);
  ASSERT_TRUE(rate_limit_rule.Init(rule));

  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  ASSERT_TRUE(rate_limit_rule.IsMatch(subset, labels));
  labels["key"] = "re111";
  ASSERT_TRUE(rate_limit_rule.IsMatch(subset, labels));

  v1::MatchString match_string;
  match_string.set_type(v1::MatchString::EXACT);
  match_string.mutable_value()->set_value("re111");
  (*rule.mutable_labels())["key"] = match_string;
  ASSERT_TRUE(rate_limit_rule.Init(rule));

  ASSERT_TRUE(rate_limit_rule.IsMatch(subset, labels));
  labels.clear();
  ASSERT_FALSE(rate_limit_rule.IsMatch(subset, labels));
}

TEST(RateLimitRuleTest, InitAmount) {
  RateLimitRule rate_limit_rule;
  v1::Rule rule;
  ASSERT_FALSE(rate_limit_rule.Init(rule));

  v1::Amount* amount = rule.add_amounts();
  amount->mutable_maxamount()->set_value(10);
  amount->mutable_validduration()->set_nanos(100);
  ASSERT_FALSE(rate_limit_rule.Init(rule));

  amount->mutable_validduration()->set_seconds(1);
  ASSERT_TRUE(rate_limit_rule.Init(rule));
}

TEST(RateLimitRuleTest, InitAction) {
  RateLimitRule rate_limit_rule;
  v1::Rule rule;
  InitRuleAmount(rule);
  ASSERT_TRUE(rate_limit_rule.Init(rule));

  rule.mutable_action()->set_value("reject");
  ASSERT_TRUE(rate_limit_rule.Init(rule));
  rule.mutable_action()->set_value("unirate");
  ASSERT_TRUE(rate_limit_rule.Init(rule));

  rule.mutable_action()->set_value("REJECT");
  ASSERT_TRUE(rate_limit_rule.Init(rule));
  rule.mutable_action()->set_value("UNIRATE");
  ASSERT_TRUE(rate_limit_rule.Init(rule));

  rule.mutable_action()->set_value("rej");
  ASSERT_FALSE(rate_limit_rule.Init(rule));
  rule.mutable_action()->set_value("uni");
  ASSERT_FALSE(rate_limit_rule.Init(rule));
}

TEST(RateLimitRuleTest, InitReport) {
  RateLimitRule rate_limit_rule;
  v1::Rule rule;
  InitRuleAmount(rule);
  ASSERT_TRUE(rate_limit_rule.Init(rule));

  rule.mutable_report()->mutable_amountpercent()->set_value(0);
  ASSERT_FALSE(rate_limit_rule.Init(rule));

  rule.mutable_report()->mutable_amountpercent()->set_value(101);
  ASSERT_FALSE(rate_limit_rule.Init(rule));

  rule.mutable_report()->mutable_amountpercent()->set_value(50);
  ASSERT_TRUE(rate_limit_rule.Init(rule));

  rule.mutable_report()->mutable_interval()->set_nanos(50 * 1000 * 1000);
  ASSERT_TRUE(rate_limit_rule.Init(rule));
  ASSERT_EQ(rate_limit_rule.GetRateLimitReport().interval_, 40);
  ASSERT_EQ(rate_limit_rule.GetRateLimitReport().jitter_, 20);
  ASSERT_GE(rate_limit_rule.GetRateLimitReport().IntervalWithJitter(), 40);
  ASSERT_LE(rate_limit_rule.GetRateLimitReport().IntervalWithJitter(), 60);
}

TEST(RateLimitRuleTest, SortByPriority) {
  RateLimitData limit_data;
  for (int i = 0; i < 10; ++i) {
    v1::Rule rule;
    InitRuleAmount(rule);
    rule.mutable_priority()->set_value((10 - i) / 3);
    rule.mutable_id()->set_value(StringUtils::TypeToStr(i));
    RateLimitRule* rate_limit_rule = new RateLimitRule();
    ASSERT_TRUE(rate_limit_rule->Init(rule));
    limit_data.AddRule(rate_limit_rule);
  }
  limit_data.SortByPriority();
  const std::vector<RateLimitRule*>& rules = limit_data.GetRules();
  for (int i = 1; i < 10; ++i) {
    ASSERT_TRUE(rules[i - 1]->GetPriority() < rules[i]->GetPriority() ||
                (rules[i - 1]->GetPriority() == rules[i]->GetPriority() &&
                 rules[i - 1]->GetId() < rules[i]->GetId()));
  }
}

TEST(RateLimitRuleTest, RegexCombine) {
  for (int i = 0; i < 2; ++i) {
    RateLimitRule rate_limit_rule;
    v1::Rule rule;
    InitRuleAmount(rule);
    rule.mutable_regex_combine()->set_value(i == 0);
    rule.mutable_id()->set_value("rule_id");
    ASSERT_TRUE(rate_limit_rule.Init(rule));

    v1::MatchString match_string;
    match_string.set_type(v1::MatchString::REGEX);
    match_string.mutable_value()->set_value("r.*");
    (*rule.mutable_subset())["subset"] = match_string;
    (*rule.mutable_labels())["label"]  = match_string;
    ASSERT_TRUE(rate_limit_rule.Init(rule));

    std::map<std::string, std::string> subset;
    std::map<std::string, std::string> labels;
    subset["subset"] = "re1";
    labels["label"]  = "reg2";
    ASSERT_TRUE(rate_limit_rule.IsMatch(subset, labels));
    ASSERT_EQ("rule_id", rate_limit_rule.GetId());
    RateLimitWindowKey window_key;
    rate_limit_rule.GetWindowKey(subset, labels, window_key);
    ASSERT_EQ(rate_limit_rule.GetMetricId(window_key), "rule_id#subset:re1#label:reg2");
  }
}

TEST(RateLimitRuleTest, RuleIndex) {
  RateLimitData limit_data;
  srand(time(NULL));
  for (int i = 0; i < 100; ++i) {
    v1::Rule rule;
    InitRuleAmount(rule);
    rule.mutable_id()->set_value(StringUtils::TypeToStr(i));
    for (int j = 0; j < 4; ++j) {
      v1::MatchString match_string;
      if (rand() % 3 != 0) {
        match_string.set_type(v1::MatchString::EXACT);
        match_string.mutable_value()->set_value("v" + StringUtils::TypeToStr(i));
      } else {
        match_string.set_type(v1::MatchString::REGEX);
        match_string.mutable_value()->set_value("v.*");
      }
      (*rule.mutable_labels())["k" + StringUtils::TypeToStr(j)] = match_string;
    }
    RateLimitRule* rate_limit_rule = new RateLimitRule();
    ASSERT_TRUE(rate_limit_rule->Init(rule));
    limit_data.AddRule(rate_limit_rule);
  }
  limit_data.SetupIndexMap();
  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  for (int i = 0; i < 1000; ++i) {
    int value    = rand() % 100;
    labels["k0"] = labels["k1"] = labels["k2"] = labels["k3"] = "v" + StringUtils::TypeToStr(value);
    RateLimitRule* rule = limit_data.MatchRule(subset, labels);
    ASSERT_TRUE(rule != NULL);
    ASSERT_TRUE(rule->IsMatch(subset, labels));
  }
}

}  // namespace polaris
