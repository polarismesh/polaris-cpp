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

#include "quota/rate_limit_window.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "metric/metric_connector.h"
#include "polaris/limit.h"
#include "quota/rate_limit_connector.h"
#include "test_context.h"
#include "test_utils.h"

namespace polaris {

class RateLimitConnectorForTest : public RateLimitConnector {
 public:
  RateLimitConnectorForTest(Reactor& reactor, Context* context) : RateLimitConnector(reactor, context, 1000, 40) {}

  std::map<std::string, RateLimitConnection*>& GetConnectionMgr() { return connection_mgr_; }

 protected:
  virtual ReturnCode SelectInstance(const ServiceKey&, const std::string& hash_key, Instance** instance) {
    *instance = new Instance(hash_key, "127.0.0.1", 8081, 100);
    return kReturnOk;
  }
};

class RateLimitWindowTest : public ::testing::Test {
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    connector_ = new RateLimitConnectorForTest(reactor_, context_);
    metric_connector_ = new MetricConnector(reactor_, nullptr);
    RateLimitWindowKey window_key;
    window_ = new RateLimitWindow(reactor_, metric_connector_, window_key);
    service_key_.namespace_ = "test";
    service_key_.name_ = "cpp.limit.service";
  }

  virtual void TearDown() {
    reactor_.Stop();
    if (connector_ != nullptr) {
      delete connector_;
      connector_ = nullptr;
    }
    if (metric_connector_ != nullptr) {
      delete metric_connector_;
      metric_connector_ = nullptr;
    }
    if (window_ != nullptr) {
      window_->DecrementRef();
      window_ = nullptr;
    }
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
  }

 protected:
  Reactor reactor_;
  RateLimitConnectorForTest* connector_;
  MetricConnector* metric_connector_;
  ServiceKey service_key_;
  RateLimitRule rate_limit_rule_;
  RateLimitWindow* window_;
  Context* context_;
};

TEST_F(RateLimitWindowTest, WindowWithLocalRule) {
  v1::Rule rule;
  rule.set_type(v1::Rule::LOCAL);
  v1::Amount* amount = rule.add_amounts();
  amount->mutable_maxamount()->set_value(10);
  amount->mutable_validduration()->set_seconds(1);
  ASSERT_TRUE(rate_limit_rule_.Init(rule));  // 本地模式10qps
  TestUtils::SetUpFakeTime();
  ASSERT_EQ(window_->Init(nullptr, &rate_limit_rule_, rate_limit_rule_.GetId(), connector_), kReturnOk);
  ASSERT_EQ(window_->WaitRemoteInit(0), kReturnOk);
  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 20; ++j) {
      QuotaResponse* response = window_->AllocateQuota(1);
      ASSERT_TRUE(response != nullptr);
      ASSERT_EQ(response->GetResultCode(), j < 10 ? kQuotaResultOk : kQuotaResultLimited) << i << " " << j;
      delete response;
    }
    TestUtils::FakeNowIncrement(1000);
  }
  TestUtils::TearDownFakeTime();
}

TEST_F(RateLimitWindowTest, WindowWithRemoteRuleSyncFailed) {
  v1::Rule rule;
  v1::Amount* amount = rule.add_amounts();
  amount->mutable_maxamount()->set_value(10);
  amount->mutable_validduration()->set_seconds(2);
  ASSERT_TRUE(rate_limit_rule_.Init(rule));  // 远程模式10qps
  ASSERT_EQ(window_->Init(nullptr, &rate_limit_rule_, rate_limit_rule_.GetId(), connector_), kReturnOk);
  TestUtils::SetUpFakeTime();
  ASSERT_EQ(window_->WaitRemoteInit(0), kReturnOk);
  reactor_.RunOnce();  // 执行初始化
  ASSERT_EQ(window_->WaitRemoteInit(0), kReturnOk);
  for (int i = 0; i < 100; i++) {
    if (i % 20 == 0) {
      TestUtils::FakeNowIncrement(2000);
    }
    QuotaResponse* response = window_->AllocateQuota(1);
    ASSERT_TRUE(response != nullptr);
    ASSERT_EQ(response->GetResultCode(), i % 20 < 10 ? kQuotaResultOk : kQuotaResultLimited) << i;
    delete response;
  }
  TestUtils::TearDownFakeTime();
}

TEST_F(RateLimitWindowTest, CheckReportSpeedUp) {
  TestUtils::SetUpFakeTime();
  for (uint32_t duration = 1; duration < 3; duration++) {
    v1::Rule rule;
    v1::Amount* amount = rule.add_amounts();
    amount->mutable_maxamount()->set_value(1000);
    amount->mutable_validduration()->set_seconds(duration);
    ASSERT_TRUE(rate_limit_rule_.Init(rule));

    RateLimitWindowKey window_key;
    RateLimitWindow* window = new RateLimitWindow(reactor_, metric_connector_, window_key);
    ASSERT_EQ(window->Init(nullptr, &rate_limit_rule_, rate_limit_rule_.GetId(), connector_), kReturnOk);
    google::protobuf::RepeatedPtrField<metric::v2::QuotaCounter> counters;
    auto counter = counters.Add();
    counter->set_counterkey(1234);
    counter->set_duration(duration);
    counter->set_left(900);
    window->OnInitResponse(counters, 0, 0);

    metric::v2::RateLimitReportResponse response;
    auto quota_left = response.add_quotalefts();
    quota_left->set_counterkey(1234);
    quota_left->set_left(1);
    bool speed_up = false;
    auto report_interval = window->OnReportResponse(response, 0, speed_up);
    if (duration == 1) {
      ASSERT_TRUE(speed_up);
      ASSERT_LT(report_interval, 40);
    } else {
      ASSERT_FALSE(speed_up);
      ASSERT_EQ(report_interval, 40);
    }
    window->DecrementRef();
  }
  TestUtils::TearDownFakeTime();
}

}  // namespace polaris
