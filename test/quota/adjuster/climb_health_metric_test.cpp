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

#include "quota/adjuster/climb_health_metric.h"

#include <gtest/gtest.h>
#include <v1/ratelimit.pb.h>

#include "quota/adjuster/climb_config.h"
#include "quota/model/rate_limit_rule.h"
#include "utils/scoped_ptr.h"

namespace polaris {

class ClimbHealthMetricTest : public ::testing::Test {
protected:
  void SetUp() {
    v1::ClimbConfig climb_config;
    v1::ClimbConfig::TriggerPolicy::ErrorRate::SpecialConfig* special_config =
        climb_config.mutable_policy()->mutable_errorrate()->add_specials();
    special_config->mutable_type()->set_value("special");
    special_config->add_errorcodes()->set_value(-1);
    special_config->mutable_errorrate()->set_value(50);
    trigger_policy_.InitPolicy(climb_config.policy());
    throttling_.InitClimbThrottling(climb_config.throttling());
    health_climb_.Set(new HealthMetricClimb(trigger_policy_, throttling_));
  }

  void TearDown() {}

  void InitLimitAmount(std::vector<RateLimitAmount>& limit_amounts);

protected:
  ClimbTriggerPolicy trigger_policy_;
  ClimbThrottling throttling_;
  ScopedPtr<HealthMetricClimb> health_climb_;
  v1::MetricResponse response_;
  std::vector<RateLimitAmount> limit_amounts_;
};

void ClimbHealthMetricTest::InitLimitAmount(std::vector<RateLimitAmount>& limit_amounts) {
  RateLimitAmount rate_limit_amount;
  rate_limit_amount.max_amount_     = 70;
  rate_limit_amount.valid_duration_ = 1000;
  rate_limit_amount.precision_      = 100;

  rate_limit_amount.start_amount_ = 70;
  rate_limit_amount.end_amount_   = 100;
  rate_limit_amount.min_amount_   = 10;
  limit_amounts.push_back(rate_limit_amount);
}

TEST_F(ClimbHealthMetricTest, NoNeedAdjust) {
  v1::MetricResponse::MetricSum* metric_sum = response_.add_summaries();
  health_climb_->Update(response_);
  // 没有配额不用调整
  ASSERT_FALSE(health_climb_->TryAdjust(limit_amounts_));

  InitLimitAmount(limit_amounts_);
  // 软限或以下，请求数太少，即使错误率足够也不调整
  v1::MetricResponse::MetricSum::Value* value = metric_sum->add_values();
  value->mutable_dimension()->set_type(v1::ReqCount);
  value->set_value(trigger_policy_.error_rate_.request_volume_threshold_);
  value = metric_sum->add_values();
  value->mutable_dimension()->set_type(v1::ErrorCount);
  value->set_value(trigger_policy_.error_rate_.request_volume_threshold_);
  health_climb_->Update(response_);
  ASSERT_FALSE(health_climb_->TryAdjust(limit_amounts_));
}

TEST_F(ClimbHealthMetricTest, NeedAdjust) {
  InitLimitAmount(limit_amounts_);
  v1::MetricResponse::MetricSum* metric_sum   = response_.add_summaries();
  v1::MetricResponse::MetricSum::Value* value = metric_sum->add_values();
  for (int i = 0; i < 3; ++i) {  // 三种类型：慢调用数 错误数 特殊错误数
    int64_t total_count = i == 0 ? 5 : trigger_policy_.error_rate_.request_volume_threshold_ + 1;
    value->mutable_dimension()->set_type(v1::ReqCount);
    value->set_value(total_count);

    value = metric_sum->add_values();
    if (i == 0) {
      value->mutable_dimension()->set_type(v1::ReqCountByDelay);
      value->mutable_dimension()->set_value("300");
      value->set_value(trigger_policy_.slow_rate_.slow_rate_ * total_count / 100);
    } else if (i == 1) {
      value->mutable_dimension()->set_type(v1::ErrorCount);
      value->set_value(trigger_policy_.error_rate_.error_rate_ * total_count / 100);
    } else {
      value->mutable_dimension()->set_type(v1::ErrorCountByType);
      value->mutable_dimension()->set_value("special");
      value->set_value((trigger_policy_.error_rate_.request_volume_threshold_ + 1) / 2);
    }
    health_climb_->Update(response_);
    ASSERT_FALSE(health_climb_->TryAdjust(limit_amounts_));
    if (i == 0) {
      value->set_value(trigger_policy_.slow_rate_.slow_rate_ * total_count / 100 + 1);
    } else if (i == 1) {
      value->set_value(trigger_policy_.error_rate_.error_rate_ * total_count / 100 + 1);
    } else {
      value->set_value(trigger_policy_.error_rate_.request_volume_threshold_ / 2 + 1);
    }
    health_climb_->Update(response_);
    ASSERT_TRUE(health_climb_->TryAdjust(limit_amounts_));
  }
}

TEST_F(ClimbHealthMetricTest, TuneUp) {
  v1::MetricResponse::MetricSum* metric_sum = response_.add_summaries();
  InitLimitAmount(limit_amounts_);
  limit_amounts_[0].max_amount_ = 10;  // 软限以下
  health_climb_->Update(response_);
  ASSERT_FALSE(health_climb_->TryAdjust(limit_amounts_));

  // 健康 且 有限流
  v1::MetricResponse::MetricSum::Value* value = metric_sum->add_values();
  value->mutable_dimension()->set_type(v1::ReqCount);
  value->set_value(100);
  value = metric_sum->add_values();
  value->mutable_dimension()->set_type(v1::LimitCount);
  for (int i = 0; i < 24; ++i) {
    value->set_value(i % 6);
    health_climb_->Update(response_);
    if (i % 6 == 0) {  // 没有限流不调整
      ASSERT_FALSE(health_climb_->TryAdjust(limit_amounts_));
    } else if (i < 6) {  // 在软限70以下每次都调整，序列 16, 25, 39, 60, 70;
      ASSERT_TRUE(health_climb_->TryAdjust(limit_amounts_));
      if (i < 5) {
        ASSERT_LT(limit_amounts_[0].max_amount_, limit_amounts_[0].start_amount_);
      } else {  // i==5时达到软限
        ASSERT_EQ(limit_amounts_[0].max_amount_, limit_amounts_[0].start_amount_);
      }
    } else {  // i > 6 软限以上调整，i = 7,8限流数不够，i=9第一次触发上调
      if (i < 10) {
        ASSERT_FALSE(health_climb_->TryAdjust(limit_amounts_));
        ASSERT_EQ(limit_amounts_[0].max_amount_, limit_amounts_[0].start_amount_);
      } else {
        if (i == 10) {  // i = 10(70->88)
          ASSERT_TRUE(health_climb_->TryAdjust(limit_amounts_));
          ASSERT_EQ(limit_amounts_[0].max_amount_, 88);
        } else if (i < 16) {  // i = 11 (88->88)次数不够，i=12,13,14限流数不够，i=15触发上调次数不够
          ASSERT_FALSE(health_climb_->TryAdjust(limit_amounts_));
          ASSERT_EQ(limit_amounts_[0].max_amount_, 88);
        } else {
          if (i == 16) {  // i =16(88->100)
            ASSERT_TRUE(health_climb_->TryAdjust(limit_amounts_));
          } else {  // i > 16(100->100)
            ASSERT_FALSE(health_climb_->TryAdjust(limit_amounts_));
          }
          ASSERT_EQ(limit_amounts_[0].max_amount_, limit_amounts_[0].end_amount_);
        }
      }
    }
  }
}

TEST_F(ClimbHealthMetricTest, TuneDown) {
  v1::MetricResponse::MetricSum* metric_sum = response_.add_summaries();
  InitLimitAmount(limit_amounts_);
  limit_amounts_[0].max_amount_ = 90;  // 软限以上
  health_climb_->Update(response_);
  ASSERT_FALSE(health_climb_->TryAdjust(limit_amounts_));

  // 不健康
  v1::MetricResponse::MetricSum::Value* value = metric_sum->add_values();
  value->mutable_dimension()->set_type(v1::ReqCount);
  value->set_value(100);
  value = metric_sum->add_values();
  value->mutable_dimension()->set_type(v1::ReqCountByDelay);
  for (int i = 0; i < 40; ++i) {
    value->set_value(i);
    health_climb_->Update(response_);
    if (i <= 20) {  // 慢调用数不够，不触发调整
      ASSERT_FALSE(health_climb_->TryAdjust(limit_amounts_));
    } else if (i <= 30) {  // 软限上下调 85, 80, 76, 72, 70
      if (i % 2 == 0) {
        uint32_t before_adjust = limit_amounts_[0].max_amount_;
        ASSERT_TRUE(health_climb_->TryAdjust(limit_amounts_));
        uint32_t after_adjust = before_adjust * throttling_.cold_above_tune_down_rate_ / 100;
        ASSERT_EQ(limit_amounts_[0].max_amount_, after_adjust > 70 ? after_adjust : 70);
      } else {
        ASSERT_FALSE(health_climb_->TryAdjust(limit_amounts_));
      }
    } else {
      if (i <= 37) {  // 52, 39, 29, 21, 15, 11, 10
        uint32_t before_adjust = limit_amounts_[0].max_amount_;
        ASSERT_TRUE(health_climb_->TryAdjust(limit_amounts_));
        uint32_t after_adjust = before_adjust * throttling_.cold_below_tune_down_rate_ / 100;
        ASSERT_EQ(limit_amounts_[0].max_amount_, after_adjust > 10 ? after_adjust : 10);
      } else {
        ASSERT_FALSE(health_climb_->TryAdjust(limit_amounts_));
        ASSERT_EQ(limit_amounts_[0].max_amount_, limit_amounts_[0].min_amount_);
      }
    }
  }
}

}  // namespace polaris
