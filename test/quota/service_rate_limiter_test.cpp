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

#include "quota/service_rate_limiter.h"

#include <gtest/gtest.h>

#include "test_utils.h"

namespace polaris {

TEST(ServiceRateLimiterTest, RejectQuotaBucket) {
  ServiceRateLimiter* limiter = ServiceRateLimiter::Create(kRateLimitActionReject);
  QuotaBucket* quota_bucket = nullptr;
  ASSERT_EQ(limiter->InitQuotaBucket(nullptr, quota_bucket), kReturnOk);
  quota_bucket->Release();
  for (int i = 0; i < 100; ++i) {
    QuotaResult* result = quota_bucket->GetQuota(1);
    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(result->result_code_, kQuotaResultOk);
    ASSERT_EQ(result->queue_time_, 0);
    delete result;
  }
  delete quota_bucket;
  delete limiter;
}

TEST(ServiceRateLimiterTest, UnirateQuotaBucketRejectAll) {
  RateLimitRule* rate_limit_rule = new RateLimitRule();
  v1::Rule rule;
  v1::Amount* amount = rule.add_amounts();
  amount->mutable_maxamount()->set_value(0);
  amount->mutable_validduration()->set_seconds(1);
  ASSERT_EQ(rate_limit_rule->Init(rule), true);
  ServiceRateLimiter* limiter = ServiceRateLimiter::Create(kRateLimitActionUnirate);
  QuotaBucket* quota_bucket = nullptr;
  ASSERT_EQ(limiter->InitQuotaBucket(rate_limit_rule, quota_bucket), kReturnOk);
  for (int i = 0; i < 100; ++i) {
    QuotaResult* result = quota_bucket->GetQuota(1);
    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(result->result_code_, kQuotaResultOk);
    ASSERT_EQ(result->queue_time_, 0);
    delete result;
  }
  delete quota_bucket;
  delete limiter;
  delete rate_limit_rule;
}

TEST(ServiceRateLimiterTest, UnirateQuotaBucket) {
  TestUtils::SetUpFakeTime();
  RateLimitRule* rate_limit_rule = new RateLimitRule();
  v1::Rule rule;
  v1::Amount* amount = rule.add_amounts();
  amount->mutable_maxamount()->set_value(150);
  amount->mutable_validduration()->set_seconds(10);
  amount = rule.add_amounts();
  amount->mutable_maxamount()->set_value(20);
  amount->mutable_validduration()->set_seconds(2);
  rule.set_type(v1::Rule::GLOBAL);  // 全局模式
  ASSERT_EQ(rate_limit_rule->Init(rule), true);
  // 10s 150个和2s 20个最终会选择2s 20个进行排队
  ServiceRateLimiter* limiter = ServiceRateLimiter::Create(kRateLimitActionUnirate);
  QuotaBucket* quota_bucket = nullptr;
  // 总体上每2000/20=100ms放一个请求
  ASSERT_EQ(limiter->InitQuotaBucket(rate_limit_rule, quota_bucket), kReturnOk);
  for (int i = 0; i < 20; ++i) {
    QuotaResult* result = quota_bucket->GetQuota(1);
    ASSERT_TRUE(result != nullptr);
    if (i < 11) {  // 第0个请求肯定不排队，1-10个请求每个排队i*100s
      ASSERT_EQ(result->result_code_, kQuotaResultOk);
      ASSERT_EQ(result->queue_time_, i * 100);
    } else if (i == 11) {  // 第11个等待超过1s直接拒绝
      ASSERT_EQ(result->result_code_, kQuotaResultLimited);
      ASSERT_EQ(result->queue_time_, 0);
      TestUtils::FakeNowIncrement(1100);  // 等待1s + 100ms
    } else {                              // 第12个开始以50ms间隔匀速进入
      ASSERT_EQ(result->result_code_, kQuotaResultOk);
      ASSERT_EQ(result->queue_time_, (i - 12) * 50);
      TestUtils::FakeNowIncrement(50);
    }
    delete result;
  }
  delete quota_bucket;
  delete limiter;
  delete rate_limit_rule;
  TestUtils::TearDownFakeTime();
}

}  // namespace polaris
