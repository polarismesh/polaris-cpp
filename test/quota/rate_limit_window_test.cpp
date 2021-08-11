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
  RateLimitConnectorForTest(Reactor& reactor, Context* context)
      : RateLimitConnector(reactor, context, 1000) {}

  std::map<std::string, RateLimitConnection*>& GetConnectionMgr() { return connection_mgr_; }

protected:
  virtual ReturnCode SelectInstance(const ServiceKey&, const std::string& hash_key,
                                    Instance** instance) {
    *instance = new Instance(hash_key, "127.0.0.1", 8081, 100);
    return kReturnOk;
  }
};

class RateLimitWindowTest : public ::testing::Test {
  virtual void SetUp() {
    context_          = TestContext::CreateContext();
    connector_        = new RateLimitConnectorForTest(reactor_, context_);
    metric_connector_ = new MetricConnector(reactor_, NULL);
    RateLimitWindowKey window_key;
    window_                 = new RateLimitWindow(reactor_, metric_connector_, window_key);
    service_key_.namespace_ = "test";
    service_key_.name_      = "cpp.limit.service";
  }

  virtual void TearDown() {
    reactor_.Stop();
    if (connector_ != NULL) {
      delete connector_;
      connector_ = NULL;
    }
    if (metric_connector_ != NULL) {
      delete metric_connector_;
      metric_connector_ = NULL;
    }
    if (window_ != NULL) {
      window_->DecrementRef();
      window_ = NULL;
    }
    if (context_ != NULL) {
      delete context_;
      context_ = NULL;
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
  ASSERT_EQ(window_->Init(NULL, &rate_limit_rule_, rate_limit_rule_.GetId(), connector_),
            kReturnOk);
  ASSERT_EQ(window_->WaitRemoteInit(0), kReturnOk);
  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 20; ++j) {
      QuotaResponse* response = window_->AllocateQuota(1);
      ASSERT_TRUE(response != NULL);
      ASSERT_EQ(response->GetResultCode(), j < 10 ? kQuotaResultOk : kQuotaResultLimited)
          << i << " " << j;
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
  ASSERT_EQ(window_->Init(NULL, &rate_limit_rule_, rate_limit_rule_.GetId(), connector_),
            kReturnOk);
  TestUtils::SetUpFakeTime();
  ASSERT_EQ(window_->WaitRemoteInit(0), kReturnOk);
  reactor_.RunOnce();  // 执行初始化
  ASSERT_EQ(window_->WaitRemoteInit(0), kReturnOk);
  for (int i = 0; i < 100; i++) {
    if (i % 20 == 0) {
      TestUtils::FakeNowIncrement(2000);
    }
    QuotaResponse* response = window_->AllocateQuota(1);
    ASSERT_TRUE(response != NULL);
    ASSERT_EQ(response->GetResultCode(), i % 20 < 10 ? kQuotaResultOk : kQuotaResultLimited) << i;
    delete response;
  }
  TestUtils::TearDownFakeTime();
}

}  // namespace polaris
