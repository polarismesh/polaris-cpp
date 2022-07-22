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

#include "quota/adjuster/climb_adjuster.h"

#include <gtest/gtest.h>

#include <memory>

#include "mock/mock_metric_connector.h"
#include "quota/quota_bucket_qps.h"
#include "reactor/reactor.h"
#include "test_utils.h"
#include "v1/code.pb.h"

namespace polaris {

class MockRemoteBucket : public RemoteAwareQpsBucket {
 public:
  explicit MockRemoteBucket(RateLimitRule *rule) : RemoteAwareQpsBucket(rule) {}

  virtual ~MockRemoteBucket() {}

  virtual void UpdateLimitAmount(const std::vector<RateLimitAmount> &amounts) { amounts_ = amounts; }

  std::vector<RateLimitAmount> amounts_;
};

class ClimbAdjusterTest : public ::testing::Test {
 protected:
  void SetUp() {
    TestUtils::SetUpFakeTime();
    climb_adjuster_ = nullptr;
    report_interval_ = 10;
    adjust_interval_ = 20;
  }

  void TearDown() {
    reactor_.Stop();
    if (climb_adjuster_ != nullptr) {
      climb_adjuster_->DecrementRef();
      climb_adjuster_ = nullptr;
    }
    TestUtils::TearDownFakeTime();
  }

 protected:
  void CreateAdjuster(bool enable_adjuster = true);

  Reactor reactor_;
  RateLimitRule rule_;
  std::unique_ptr<MockMetricConnector> metric_connector_;
  std::unique_ptr<MockRemoteBucket> remote_bucket_;
  ClimbAdjuster *climb_adjuster_;
  uint64_t report_interval_;
  uint64_t adjust_interval_;
};

void ClimbAdjusterTest::CreateAdjuster(bool enable_adjuster) {
  v1::Rule rule;
  v1::Amount *amount = rule.add_amounts();
  amount->mutable_maxamount()->set_value(10);
  amount->mutable_validduration()->set_seconds(1);
  v1::ClimbConfig *climb_config = rule.mutable_adjuster()->mutable_climb();
  climb_config->mutable_enable()->set_value(enable_adjuster);
  climb_config->mutable_metric()->mutable_reportinterval()->set_seconds(report_interval_);
  climb_config->mutable_throttling()->mutable_judgeduration()->set_seconds(adjust_interval_);
  EXPECT_TRUE(rule_.Init(rule));

  metric_connector_.reset(new MockMetricConnector(reactor_, nullptr));
  remote_bucket_.reset(new MockRemoteBucket(&rule_));
  climb_adjuster_ = new ClimbAdjuster(reactor_, metric_connector_.get(), remote_bucket_.get());
}

TEST_F(ClimbAdjusterTest, AdjusterNotEnable) {
  CreateAdjuster(false);
  ASSERT_EQ(climb_adjuster_->Init(&rule_), kReturnInvalidConfig);
}

TEST_F(ClimbAdjusterTest, SetupTimingTaskAfterDelete) {
  CreateAdjuster();
  ASSERT_EQ(climb_adjuster_->Init(&rule_), kReturnOk);
  climb_adjuster_->IncrementRef();
  climb_adjuster_->MakeDeleted();
  ASSERT_TRUE(climb_adjuster_->IsDeleted());
  reactor_.RunOnce();
}

TEST_F(ClimbAdjusterTest, DeleteAfterSetupTimingTask) {
  CreateAdjuster();
  ASSERT_EQ(climb_adjuster_->Init(&rule_), kReturnOk);
  climb_adjuster_->IncrementRef();
  reactor_.RunOnce();
  climb_adjuster_->MakeDeleted();
  ASSERT_TRUE(climb_adjuster_->IsDeleted());
  reactor_.RunOnce();
}

TEST_F(ClimbAdjusterTest, Report) {
  CreateAdjuster();
  ASSERT_EQ(climb_adjuster_->Init(&rule_), kReturnOk);
  reactor_.RunOnce();  // 提交了定时上报和调整任务
  EXPECT_CALL(*metric_connector_, IsMetricInit(::testing::_)).WillOnce(::testing::Return(false));
  EXPECT_CALL(*metric_connector_, Initialize(::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(
          ::testing::Invoke(metric_connector_.get(), &MockMetricConnector::OnResponse<v1::MetricInitRequest>),
          ::testing::Return(kReturnOk)));
  TestUtils::FakeNowIncrement(report_interval_ * 1000);
  reactor_.RunOnce();  // 执行上报任务
  EXPECT_CALL(*metric_connector_, IsMetricInit(::testing::_)).WillOnce(::testing::Return(true));
  EXPECT_CALL(*metric_connector_, Report(::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(
          ::testing::Invoke(metric_connector_.get(), &MockMetricConnector::OnResponse<v1::MetricRequest>),
          ::testing::Return(kReturnOk)));
  TestUtils::FakeNowIncrement(2000);  // 重试上报任务
  reactor_.RunOnce();
}

TEST_F(ClimbAdjusterTest, Query) {
  report_interval_ = 20;
  adjust_interval_ = 10;
  CreateAdjuster();
  ASSERT_EQ(climb_adjuster_->Init(&rule_), kReturnOk);
  reactor_.RunOnce();  // 提交了定时上报和调整任务
  TestUtils::FakeNowIncrement(adjust_interval_ * 1000);
  EXPECT_CALL(*metric_connector_, IsMetricInit(::testing::_)).WillOnce(::testing::Return(false));
  EXPECT_CALL(*metric_connector_, Initialize(::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(
          ::testing::Invoke(metric_connector_.get(), &MockMetricConnector::OnResponse<v1::MetricInitRequest>),
          ::testing::Return(kReturnOk)));
  reactor_.RunOnce();
  TestUtils::FakeNowIncrement(2000);
  EXPECT_CALL(*metric_connector_, IsMetricInit(::testing::_)).WillOnce(::testing::Return(true));
  EXPECT_CALL(*metric_connector_, Query(::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(
          ::testing::Invoke(metric_connector_.get(), &MockMetricConnector::OnResponse<v1::MetricQueryRequest>),
          ::testing::Return(kReturnOk)));
  reactor_.RunOnce();
}

}  // namespace polaris
