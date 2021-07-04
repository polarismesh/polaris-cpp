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

#include "quota/adjuster/climb_call_metric.h"

#include <gtest/gtest.h>
#include <v1/metric.pb.h>
#include <v1/ratelimit.pb.h>

#include "quota/adjuster/climb_config.h"
#include "test_utils.h"
#include "utils/scoped_ptr.h"

namespace polaris {

class ClimbCallMetricTest : public ::testing::TestWithParam<bool> {
protected:
  void SetUp() {
    v1::ClimbConfig climb_config;
    metric_config_.InitMetricConfig(climb_config.metric());
    if (GetParam()) {
      v1::ClimbConfig::TriggerPolicy::ErrorRate::SpecialConfig* special_config =
          climb_config.mutable_policy()->mutable_errorrate()->add_specials();
      special_config->mutable_type()->set_value("special1");
      special_config->add_errorcodes()->set_value(-100);
      special_config->mutable_errorrate()->set_value(10);
    }
    trigger_policy_.InitPolicy(climb_config.policy());
    metric_data_.Set(new CallMetricData(metric_config_, trigger_policy_));
    TestUtils::SetUpFakeTime();
  }

  void TearDown() { TestUtils::TearDownFakeTime(); }

protected:
  ClimbMetricConfig metric_config_;
  ClimbTriggerPolicy trigger_policy_;
  ScopedPtr<CallMetricData> metric_data_;
};

TEST_P(ClimbCallMetricTest, RecordAndSerialize) {
  bool with_error_type = GetParam();
  TestUtils::FakeNowIncrement(100);
  metric_data_->Record(kLimitCallResultOk, 10, 0);
  metric_data_->Record(kLimitCallResultOk, 5000, 0);
  metric_data_->Record(kLimitCallResultLimited, 100, 0);
  metric_data_->Record(kLimitCallResultFailed, 1000, 0);
  metric_data_->Record(kLimitCallResultFailed, 100, -100);  // special error code

  v1::MetricRequest metric_request;
  metric_data_->Serialize(&metric_request);
  ASSERT_EQ(metric_request.increments_size(), 1);
  const v1::MetricRequest::MetricIncrement& increment = metric_request.increments(0);
  ASSERT_EQ(increment.values_size(), with_error_type ? 5 : 4);
  ASSERT_EQ(increment.values(0).dimension().type(), v1::ReqCount);
  ASSERT_EQ(increment.values(0).values(0), 5);

  ASSERT_EQ(increment.values(1).dimension().type(), v1::LimitCount);
  ASSERT_EQ(increment.values(1).values(0), 1);

  ASSERT_EQ(increment.values(2).dimension().type(), v1::ReqCountByDelay);
  ASSERT_TRUE(!increment.values(2).dimension().value().empty());
  ASSERT_EQ(increment.values(2).values(0), 1);

  ASSERT_EQ(increment.values(3).dimension().type(), v1::ErrorCount);
  ASSERT_EQ(increment.values(3).values(0), with_error_type ? 1 : 2);
  if (with_error_type) {
    ASSERT_EQ(increment.values(4).dimension().type(), v1::ErrorCountByType);
    ASSERT_EQ(increment.values(4).dimension().value(), "special1");
    ASSERT_EQ(increment.values(4).values(0), 1);
  }
}

INSTANTIATE_TEST_CASE_P(TestGroup, ClimbCallMetricTest, ::testing::Bool());

}  // namespace polaris
