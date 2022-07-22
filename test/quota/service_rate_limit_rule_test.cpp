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

#include "mock/fake_server_response.h"
#include "polaris/model.h"

namespace polaris {

class ServiceLimitRuleMatchTest : public ::testing::Test {
  virtual void SetUp() {
    service_key_.namespace_ = "Test";
    service_key_.name_ = "test.rate.limit.match";

    service_rule_ = CreateRateLimitRule();
    ASSERT_TRUE(service_rule_ != nullptr);
  }

  virtual void TearDown() {
    if (service_rule_ != nullptr) {
      delete service_rule_;
      service_rule_ = nullptr;
    }
  }

 protected:
  ServiceRateLimitRule* CreateRateLimitRule() {
    response_.mutable_code()->set_value(v1::ExecuteSuccess);
    response_.set_type(v1::DiscoverResponse::RATE_LIMIT);
    FakeServer::SetService(response_, service_key_);

    v1::MatchString exact_value;
    exact_value.set_type(v1::MatchString::EXACT);
    v1::MatchString regex_value;
    regex_value.set_type(v1::MatchString::REGEX);

    // k1:v1  k2:v2a.*
    exact_value.mutable_value()->set_value("v1");
    regex_value.mutable_value()->set_value("v2a.*");
    AddRule(response_.mutable_ratelimit()->add_rules(), "1", "k1", exact_value, "k2", regex_value);

    // k1:v1  k2:v2b.*
    exact_value.mutable_value()->set_value("v1");
    regex_value.mutable_value()->set_value("v2b.*");
    AddRule(response_.mutable_ratelimit()->add_rules(), "2", "k1", exact_value, "k2", regex_value);

    // k1:v1a.*  k2:v2
    regex_value.mutable_value()->set_value("v1a.*");
    exact_value.mutable_value()->set_value("v2");
    AddRule(response_.mutable_ratelimit()->add_rules(), "3", "k1", regex_value, "k2", exact_value);

    // k1:v1b.*  k2:v2
    regex_value.mutable_value()->set_value("v1b.*");
    exact_value.mutable_value()->set_value("v2");
    AddRule(response_.mutable_ratelimit()->add_rules(), "4", "k1", regex_value, "k2", exact_value);

    // k2:v2  k3:v3a.*
    exact_value.mutable_value()->set_value("v2");
    regex_value.mutable_value()->set_value("v3a.*");
    AddRule(response_.mutable_ratelimit()->add_rules(), "5", "k2", exact_value, "k3", regex_value);

    // k2:v2  k3:v3b.*
    exact_value.mutable_value()->set_value("v2");
    regex_value.mutable_value()->set_value("v3b.*");
    AddRule(response_.mutable_ratelimit()->add_rules(), "6", "k2", exact_value, "k3", regex_value);

    // 需要20条规则以上才会建立索引
    for (int i = 6; i < 25; i++) {
      std::string id = std::to_string(i);
      exact_value.mutable_value()->set_value("v" + id);
      regex_value.mutable_value()->set_value("v" + id + ".*");
      AddRule(response_.mutable_ratelimit()->add_rules(), id, "k1", exact_value, "k2", regex_value);
    }

    ServiceData* service_data = ServiceData::CreateFromPb(&response_, kDataIsSyncing, 0);
    EXPECT_TRUE(service_data != nullptr);
    ServiceRateLimitRule* rule = new ServiceRateLimitRule(service_data);
    return rule;
  }

  void AddRule(v1::Rule* rule, const std::string& id, const std::string& key1, const v1::MatchString& value1,
               const std::string& key2, const v1::MatchString& value2) {
    rule->mutable_id()->set_value(id);
    (*rule->mutable_labels())[key1] = value1;
    (*rule->mutable_labels())[key2] = value2;
    v1::Amount* amount = rule->add_amounts();
    amount->mutable_maxamount()->set_value(100);
    amount->mutable_validduration()->set_seconds(1);
  }

 protected:
  ServiceRateLimitRule* service_rule_;
  ServiceKey service_key_;
  v1::DiscoverResponse response_;
};

TEST_F(ServiceLimitRuleMatchTest, MatchRule) {
  std::map<std::string, std::string> subset, labels;
  RateLimitRule* rule;

  // k1:v1  k2:v2a.*
  labels.clear();
  labels["k1"] = "v1";
  labels["k2"] = "v2aa";
  rule = service_rule_->MatchRateLimitRule(subset, labels);
  ASSERT_TRUE(rule != nullptr);
  ASSERT_EQ(rule->GetId(), "1");

  // k1:v1  k2:v2b.*
  labels.clear();
  labels["k1"] = "v1";
  labels["k2"] = "v2b";
  rule = service_rule_->MatchRateLimitRule(subset, labels);
  ASSERT_TRUE(rule != nullptr);
  ASSERT_EQ(rule->GetId(), "2");

  // k1:v1a.*  k2:v2
  labels.clear();
  labels["k1"] = "v1a";
  labels["k2"] = "v2";
  rule = service_rule_->MatchRateLimitRule(subset, labels);
  ASSERT_TRUE(rule != nullptr);
  ASSERT_EQ(rule->GetId(), "3");

  // k1:v1b.*  k2:v2
  labels.clear();
  labels["k1"] = "v1b";
  labels["k2"] = "v2";
  rule = service_rule_->MatchRateLimitRule(subset, labels);
  ASSERT_TRUE(rule != nullptr);
  ASSERT_EQ(rule->GetId(), "4");

  // k2:v2  k3:v3a.*
  labels.clear();
  labels["k2"] = "v2";
  labels["k3"] = "v3a";
  rule = service_rule_->MatchRateLimitRule(subset, labels);
  ASSERT_TRUE(rule != nullptr);
  ASSERT_EQ(rule->GetId(), "5");

  // k2:v2  k3:v3b.*
  labels.clear();
  labels["k2"] = "v2";
  labels["k3"] = "v3b";
  rule = service_rule_->MatchRateLimitRule(subset, labels);
  ASSERT_TRUE(rule != nullptr);
  ASSERT_EQ(rule->GetId(), "6");

  for (int i = 6; i < 25; i++) {
    std::string id = std::to_string(i);
    labels.clear();
    labels["k1"] = "v" + id;
    labels["k2"] = "v" + id + id;
    rule = service_rule_->MatchRateLimitRule(subset, labels);
    ASSERT_TRUE(rule != nullptr);
    ASSERT_EQ(rule->GetId(), id);
  }
}

TEST_F(ServiceLimitRuleMatchTest, CheckRuleEnable) {
  std::map<std::string, std::string> subset, labels;

  // 获取第1条规则 k1:v1  k2:v2a.*
  labels["k1"] = "v1";
  labels["k2"] = "v2aa";
  RateLimitRule* rule1 = service_rule_->MatchRateLimitRule(subset, labels);
  ASSERT_TRUE(rule1 != nullptr);
  ASSERT_EQ(rule1->GetId(), "1");

  // 获取第2条规则 k1:v1  k2:v2b.*
  labels.clear();
  labels["k1"] = "v1";
  labels["k2"] = "v2b";
  RateLimitRule* rule2 = service_rule_->MatchRateLimitRule(subset, labels);
  ASSERT_TRUE(rule2 != nullptr);
  ASSERT_EQ(rule2->GetId(), "2");
  // 修改第2条规则的版本号
  v1::Rule* pb_rule = response_.mutable_ratelimit()->mutable_rules(1);
  pb_rule->mutable_revision()->set_value("new_revision");
  ServiceData* service_data = ServiceData::CreateFromPb(&response_, kDataIsSyncing, 0);
  EXPECT_TRUE(service_data != nullptr);
  ServiceRateLimitRule service_rule2(service_data);
  ASSERT_TRUE(service_rule2.IsRuleEnable(rule1));   // 原先的规则1正常
  ASSERT_FALSE(service_rule2.IsRuleEnable(rule2));  // 原先的规则2已经失效

  // 获取第3条规则 k1:v1a.*  k2:v2
  labels.clear();
  labels["k1"] = "v1a";
  labels["k2"] = "v2";
  RateLimitRule* rule3 = service_rule_->MatchRateLimitRule(subset, labels);
  ASSERT_TRUE(rule3 != nullptr);
  ASSERT_EQ(rule3->GetId(), "3");
  // 屏蔽第3条规则
  pb_rule = response_.mutable_ratelimit()->mutable_rules(2);
  pb_rule->mutable_disable()->set_value(true);
  service_data = ServiceData::CreateFromPb(&response_, kDataIsSyncing, 0);
  EXPECT_TRUE(service_data != nullptr);
  ServiceRateLimitRule service_rule3(service_data);
  ASSERT_TRUE(service_rule3.IsRuleEnable(rule1));   // 原先的规则1正常
  ASSERT_FALSE(service_rule3.IsRuleEnable(rule3));  // 原先的规则3已经失效

  // 获取第4条规则 k1:v1b.*  k2:v2
  labels.clear();
  labels["k1"] = "v1b";
  labels["k2"] = "v2";
  RateLimitRule* rule4 = service_rule_->MatchRateLimitRule(subset, labels);
  ASSERT_TRUE(rule4 != nullptr);
  ASSERT_EQ(rule4->GetId(), "4");
  // 第4条规则被删除，通过修改规则4的ID模拟被删除
  pb_rule = response_.mutable_ratelimit()->mutable_rules(3);
  pb_rule->mutable_id()->set_value("4444");
  service_data = ServiceData::CreateFromPb(&response_, kDataIsSyncing, 0);
  EXPECT_TRUE(service_data != nullptr);
  ServiceRateLimitRule service_rule4(service_data);
  ASSERT_TRUE(service_rule4.IsRuleEnable(rule1));   // 原先的规则1正常
  ASSERT_FALSE(service_rule4.IsRuleEnable(rule4));  // 原先的规则4已经失效
}

}  // namespace polaris
