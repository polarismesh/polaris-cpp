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

#include "quota/quota_bucket_qps.h"

#include <gtest/gtest.h>

#include "polaris/limit.h"
#include "quota/rate_limit_window.h"
#include "test_utils.h"
#include "utils/time_clock.h"

namespace polaris {

class TokenBucketTest : public ::testing::Test {
protected:
  void SetUp() {
    RateLimitAmount amount;
    amount.max_amount_     = 10;
    amount.valid_duration_ = 1000;
    token_bucket_.Init(amount, Time::GetCurrentTimeMs(), 5);
    acquire_amount_ = 1;
  }

  void TearDown() {}

protected:
  TokenBucket token_bucket_;
  int64_t acquire_amount_;
};

TEST_F(TokenBucketTest, LocalUsage) {
  int64_t left_quota;
  uint64_t expect_bucket_time = Time::GetCurrentTimeMs() / 1000;
  for (int i = 0; i < 10; ++i) {
    bool result = token_bucket_.GetToken(acquire_amount_, expect_bucket_time, false, left_quota);
    if (i < 5) {
      ASSERT_EQ(result, true);
    } else {
      ASSERT_EQ(result, false);
      ASSERT_EQ(left_quota, -1);
      token_bucket_.ReturnToken(acquire_amount_, false);
    }
  }
}

TEST_F(TokenBucketTest, RemoteUsage) {
  uint64_t expect_bucket_time = Time::GetCurrentTimeMs() / 1000;
  // 已经使用完2次，还剩8次
  token_bucket_.RefreshToken(8, 0, expect_bucket_time, false, 0);
  for (int i = 0; i < 10; ++i) {
    int64_t left_quota;
    bool result = token_bucket_.GetToken(acquire_amount_, expect_bucket_time, true, left_quota);
    if (i < 8) {
      ASSERT_EQ(result, true);
    } else {
      ASSERT_EQ(result, false);
      token_bucket_.ReturnToken(acquire_amount_, true);
    }
  }
}

TEST_F(TokenBucketTest, RefreshToken) {
  uint64_t expect_bucket_time = Time::GetCurrentTimeMs() / 1000;
  // 已经使用完0次，还剩10
  token_bucket_.RefreshToken(10, 0, expect_bucket_time, false, 0);
  for (int i = 0; i < 20; ++i) {
    int64_t left_quota;
    bool result = token_bucket_.GetToken(acquire_amount_, expect_bucket_time, true, left_quota);
    if (i < 10) {
      ASSERT_EQ(result, true);
    } else {
      ASSERT_EQ(result, false);
      token_bucket_.ReturnToken(acquire_amount_, true);
    }
    if (i == 3) {
      QuotaUsage quota_usage;
      token_bucket_.PreparePendingQuota(expect_bucket_time, quota_usage);
      ASSERT_EQ(quota_usage.quota_allocated_, 4);
    }
    if (i == 4) {
      // 已经使用完4次，还剩6
      token_bucket_.RefreshToken(6, 4, expect_bucket_time, false, 0);
    }
  }
}

TEST_F(TokenBucketTest, RefreshTokenWithLeft) {
  uint64_t current_time       = Time::GetCurrentTimeMs();
  uint64_t expect_bucket_time = current_time / 1000;
  // 还剩10次，不需要加快上报
  uint64_t report_time = token_bucket_.RefreshToken(10, 0, expect_bucket_time, false, 0);
  ASSERT_EQ(report_time, 0);
  for (int i = 0; i < 20; ++i) {
    int64_t left_quota;
    bool result = token_bucket_.GetToken(acquire_amount_, expect_bucket_time, true, left_quota);
    QuotaUsage quota_usage;
    token_bucket_.PreparePendingQuota(expect_bucket_time, quota_usage);
    if (i < 7) {
      ASSERT_EQ(result, true);
      ASSERT_EQ(quota_usage.quota_allocated_, 1);
    } else {
      ASSERT_EQ(result, false);
      token_bucket_.ReturnToken(acquire_amount_, true);
      ASSERT_EQ(quota_usage.quota_rejected_, 1);
    }
    if (i == 2) {
      // 远端还剩6次，本地已上报2次，本地共使用3次，还剩5次
      // 80ms消耗5次，剩余需要80ms，还无需加快上报
      report_time = token_bucket_.RefreshToken(6, 2, expect_bucket_time, false, 80);
      ASSERT_EQ(report_time, 0);
    }
    if (i == 3) {
      // 远端还剩4次，本地又上报1次，共使用4次，100ms消耗7次，需要42ms，需加快上报
      report_time = token_bucket_.RefreshToken(4, 1, expect_bucket_time, false, 100);
      ASSERT_EQ(report_time, 22);
    }
  }
}

class QuotaBucketQpsTest : public ::testing::Test {
protected:
  void SetUp() {
    TestUtils::SetUpFakeTime();
    qps_bucket_ = NULL;
    RateLimitRule rate_limit_rule;
    InitRateLimitRule(rate_limit_rule);
    qps_bucket_     = new RemoteAwareQpsBucket(&rate_limit_rule);
    acquire_amount_ = 1;
  }

  void TearDown() {
    if (qps_bucket_ != NULL) {
      delete qps_bucket_;
      qps_bucket_ = NULL;
    }
    TestUtils::TearDownFakeTime();
  }

protected:
  static void InitRateLimitRule(RateLimitRule &rate_limit_rule);
  RemoteAwareQpsBucket *qps_bucket_;
  int64_t acquire_amount_;
};

void QuotaBucketQpsTest::InitRateLimitRule(RateLimitRule &rate_limit_rule) {
  v1::Rule rule;
  rule.set_type(v1::Rule_Type_GLOBAL);
  rule.mutable_report()->mutable_amountpercent()->set_value(40);
  v1::Amount *amount = rule.add_amounts();
  amount->mutable_maxamount()->set_value(10);
  amount->mutable_validduration()->set_seconds(1);
  EXPECT_TRUE(rate_limit_rule.Init(rule));
}

TEST_F(QuotaBucketQpsTest, AllocateMulti) {
  LimitAllocateResult limit_result;
  QuotaResponse *response =
      qps_bucket_->Allocate(acquire_amount_, Time::GetCurrentTimeMs(), &limit_result);
  ASSERT_EQ(response->GetResultCode(), kQuotaResultOk);
  ASSERT_EQ(limit_result.violate_duration_, 0);
  delete response;
  acquire_amount_ = 39;
  response        = qps_bucket_->Allocate(acquire_amount_, Time::GetCurrentTimeMs(), &limit_result);
  ASSERT_EQ(response->GetResultCode(), kQuotaResultLimited);
  ASSERT_EQ(limit_result.violate_duration_, 1000);
  delete response;
}

TEST_F(QuotaBucketQpsTest, AllocateBeforeInit) {
  LimitAllocateResult limit_result;
  for (int i = 0; i < 20; ++i) {
    QuotaResponse *response =
        qps_bucket_->Allocate(acquire_amount_, Time::GetCurrentTimeMs(), &limit_result);
    ASSERT_EQ(response->GetResultCode(), i < 10 ? kQuotaResultOk : kQuotaResultLimited);
    ASSERT_EQ(limit_result.violate_duration_, i < 10 ? 0 : 1000);
    delete response;
  }
}

TEST_F(QuotaBucketQpsTest, AllocateWithExpired) {
  LimitAllocateResult limit_result;
  for (int j = 0; j < 10; ++j) {
    if (j == 5) {  // 完成Init
      RemoteQuotaResult result;
      result.local_usage_                                      = NULL;
      result.curret_server_time_                               = Time::GetCurrentTimeMs();
      result.remote_usage_.create_server_time_                 = result.curret_server_time_;
      result.remote_usage_.quota_usage_[1000].quota_allocated_ = 10;
      qps_bucket_->SetRemoteQuota(result);
    }
    for (int i = 0; i < 20; ++i) {
      QuotaResponse *response =
          qps_bucket_->Allocate(acquire_amount_, Time::GetCurrentTimeMs(), &limit_result);
      ASSERT_EQ(response->GetResultCode(), i < 10 ? kQuotaResultOk : kQuotaResultLimited);
      delete response;
    }
    TestUtils::FakeNowIncrement(1000);  // quota未更新会过期
  }
}

TEST_F(QuotaBucketQpsTest, AllocateAfterInit) {
  // 完成Init
  RemoteQuotaResult result;
  result.local_usage_                                      = NULL;
  result.curret_server_time_                               = Time::GetCurrentTimeMs();
  result.remote_usage_.create_server_time_                 = result.curret_server_time_;
  result.remote_usage_.quota_usage_[1000].quota_allocated_ = 5;
  qps_bucket_->SetRemoteQuota(result);
  LimitAllocateResult limit_result;
  for (int i = 0; i < 10; ++i) {
    QuotaResponse *response =
        qps_bucket_->Allocate(acquire_amount_, Time::GetCurrentTimeMs(), &limit_result);
    ASSERT_EQ(response->GetResultCode(), i < 5 ? kQuotaResultOk : kQuotaResultLimited);
    ASSERT_EQ(limit_result.violate_duration_, i < 5 ? 0 : 1000);
    delete response;
    if (i == 1) {  // 第40%*5次时触发上报
      uint64_t current_server_time = Time::GetCurrentTimeMs();
      QuotaUsageInfo *usage        = qps_bucket_->GetQuotaUsage(current_server_time);
      ASSERT_EQ(usage->create_server_time_, current_server_time);
      ASSERT_EQ(usage->quota_usage_.size(), 1);
      ASSERT_EQ(usage->quota_usage_[1000].quota_allocated_, 2);
      delete usage;
    }
  }
}

}  // namespace polaris
