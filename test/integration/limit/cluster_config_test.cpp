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

#include <gtest/gtest.h>
#include <pthread.h>

#include "polaris/limit.h"

#include "integration/common/integration_base.h"
#include "utils/time_clock.h"

#include "v1/ratelimit.pb.h"

namespace polaris {

class RateClusterConfigTest : public IntegrationBase {
 protected:
  RateClusterConfigTest() : limit_api_(nullptr) {}

  virtual void SetUp() {
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("cpp.integration.rate.rule.cluster" + std::to_string(Time::GetSystemTimeMs()));
    IntegrationBase::SetUp();
    CreateRateLimitRule();  // 创建限流规则

    limit_api_ = LimitApi::Create(context_);
    ASSERT_TRUE(limit_api_ != nullptr);
    sleep(3);  // 等待Discover服务器获取到服务信息
  }

  virtual void TearDown() {
    if (limit_api_ != nullptr) {
      delete limit_api_;
      limit_api_ = nullptr;
    }
    if (!rule_id_.empty()) {  // 删除限流规则
      IntegrationBase::DeleteRateLimitRule(rule_id_, service_token_);
    }
    IntegrationBase::TearDown();
  }

  // 创建限流规则数据
  void SetRateLimitRule(v1::Rule& rule);

  // 创建限流规则
  void CreateRateLimitRule();

  // 更新限流规则
  void UpdateRateLimitRule(const std::string& cluster);

  static bool CheckDegrade(LimitApi* limit_api, const QuotaRequest& request) {
    QuotaResponse* response = nullptr;
    EXPECT_EQ(limit_api->GetQuota(request, response), kReturnOk);
    EXPECT_EQ(response->GetResultCode(), kQuotaResultOk);
    bool is_degrade = response->GetQuotaResultInfo().is_degrade_;
    delete response;
    return is_degrade;
  }

 protected:
  LimitApi* limit_api_;
  std::string rule_id_;
};

void RateClusterConfigTest::SetRateLimitRule(v1::Rule& rule) {
  rule.mutable_namespace_()->set_value(service_.namespace_().value());
  rule.mutable_service()->set_value(service_.name().value());
  rule.mutable_service_token()->set_value(service_token_);

  v1::MatchString match_string;
  match_string.set_type(v1::MatchString::REGEX);
  match_string.mutable_value()->set_value(".*");
  (*rule.mutable_labels())["label"] = match_string;

  v1::Amount* amount = rule.add_amounts();
  amount->mutable_validduration()->set_seconds(1);
  amount->mutable_maxamount()->set_value(100);
  amount = rule.add_amounts();  // 添加一个10s的限流值避免窗口过期
  amount->mutable_validduration()->set_seconds(10);
  amount->mutable_maxamount()->set_value(10000);
}

void RateClusterConfigTest::CreateRateLimitRule() {
  v1::Rule rule;
  SetRateLimitRule(rule);
  IntegrationBase::CreateRateLimitRule(rule, rule_id_);
}

void RateClusterConfigTest::UpdateRateLimitRule(const std::string& cluster) {
  v1::Rule rule;
  SetRateLimitRule(rule);
  rule.mutable_cluster()->mutable_namespace_()->set_value("Polaris");
  rule.mutable_cluster()->mutable_service()->set_value(cluster);
  ASSERT_TRUE(!rule_id_.empty());
  rule.mutable_id()->set_value(rule_id_);
  IntegrationBase::UpdateRateLimitRule(rule);
}

TEST_F(RateClusterConfigTest, ChangeCluster) {
  QuotaRequest request;
  request.SetServiceNamespace(service_.namespace_().value());
  request.SetServiceName(service_.name().value());
  std::map<std::string, std::string> labels;
  labels.insert(std::make_pair("label", "label"));
  request.SetLabels(labels);

  ASSERT_EQ(CheckDegrade(limit_api_, request), false);  // 触发新建窗口
  sleep(2);
  // 由于规则里没有设置限流集群，降级到本地限流
  ASSERT_EQ(CheckDegrade(limit_api_, request), true);

  UpdateRateLimitRule("polaris.metric.test");
  sleep(5);
  ASSERT_EQ(CheckDegrade(limit_api_, request), false);  // 触发新建窗口
  sleep(2);
  // 更新规则设置限流集群，可以正常同步，不再降级
  ASSERT_EQ(CheckDegrade(limit_api_, request), false);

  UpdateRateLimitRule("polaris.metric.xxxxxxx");
  sleep(5);
  ASSERT_EQ(CheckDegrade(limit_api_, request), false);  // 触发新建窗口
  sleep(2);
  // 更新规则设置不存在的限流集群，降级到本地限流
  ASSERT_EQ(CheckDegrade(limit_api_, request), true);
}

}  // namespace polaris
