//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
//  language governing permissions and limitations under the License.
//

#include "quota/service_rate_limiter.h"

#include <stddef.h>
#include <v1/ratelimit.pb.h>

#include <memory>
#include <vector>

#include "logger.h"
#include "utils/time_clock.h"

namespace polaris {

ServiceRateLimiter* ServiceRateLimiter::Create(RateLimitActionType action_type) {
  switch (action_type) {
    case kRateLimitActionReject:
      return new RejectServiceRateLimiter();
    case kRateLimitActionUnirate:
      return new UnirateServiceRateLimiter();
    default:
      POLARIS_ASSERT(false);
  }
  return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// 直接拒绝

QuotaResult* RejectQuotaBucket::GetQuota(int64_t /*acquire_amount*/) { return new QuotaResult(kQuotaResultOk, 0); }

ReturnCode RejectServiceRateLimiter::InitQuotaBucket(RateLimitRule* /*rate_limit_rule*/, QuotaBucket*& quota_bucket) {
  quota_bucket = new RejectQuotaBucket();
  return kReturnOk;
}

///////////////////////////////////////////////////////////////////////////////
// 匀速排队

UnirateQuotaBucket::UnirateQuotaBucket()
    : rule_(nullptr),
      max_queuing_duration_(0),
      effective_amount_(0),
      effective_duration_(0),
      effective_rate_(0),
      last_grant_time_(0),
      reject_all_(false) {}

UnirateQuotaBucket::~UnirateQuotaBucket() { rule_ = nullptr; }

ReturnCode UnirateQuotaBucket::Init(RateLimitRule* rate_limit_rule) {
  rule_ = rate_limit_rule;
  max_queuing_duration_ = 1000;  // TODO 支持配置，当前默认1s

  // 选出允许qps最低的amount和duration组合，作为effective_amount和effective_duration
  // 在匀速排队限流器里面，就是每个请求都要间隔同样的时间，
  // 如限制1s 10个请求，那么每个请求只有在上个请求允许过去100ms后才能通过下一个请求
  // 这种机制下面，那么在多个amount组合里面，只要允许qps最低的组合生效，那么所有限制都满足了
  const std::vector<RateLimitAmount>& amounts = rule_->GetRateLimitAmount();
  POLARIS_ASSERT(amounts.size() > 0);
  std::size_t max_rate_index = 0;
  float max_rate = -1;
  uint64_t max_duration = 0;
  for (std::size_t i = 0; i < amounts.size(); ++i) {
    if (amounts[i].max_amount_ == 0) {
      reject_all_ = true;
      return kReturnOk;
    }
    float new_rate = static_cast<float>(amounts[i].valid_duration_) / amounts[i].max_amount_;
    if (new_rate > max_rate) {
      max_rate = new_rate;
      max_rate_index = i;
    }
    if (amounts[i].valid_duration_ > max_duration) {
      max_duration = amounts[i].valid_duration_;
    }
  }
  effective_amount_ = amounts[max_rate_index].max_amount_;
  effective_duration_ = amounts[max_rate_index].valid_duration_;
  effective_rate_ = static_cast<uint64_t>(max_rate);
  last_grant_time_ = Time::GetSystemTimeMs() - max_duration;
  return kReturnOk;
}

QuotaResult* UnirateQuotaBucket::GetQuota(int64_t acquire_amount) {
  if (reject_all_) {
    return new QuotaResult(kQuotaResultOk, 0);
  }
  // TODO 多线程支持，原子读写 + CAS实现
  uint64_t current_time = Time::GetSystemTimeMs();
  uint64_t expect_time = last_grant_time_ + effective_rate_ * acquire_amount;
  if (expect_time < current_time) {
    last_grant_time_ = current_time;
    return new QuotaResult(kQuotaResultOk, 0);
  }
  uint64_t next_grand_time = last_grant_time_ + effective_rate_ * acquire_amount;
  uint64_t wait_time = next_grand_time > current_time ? next_grand_time - current_time : 0;
  if (wait_time > max_queuing_duration_) {  // 超过最大等待时间，直接拒绝
    return new QuotaResult(kQuotaResultLimited, 0);
  }
  last_grant_time_ = next_grand_time;
  return new QuotaResult(kQuotaResultOk, wait_time);
}

ReturnCode UnirateServiceRateLimiter::InitQuotaBucket(RateLimitRule* rate_limit_rule, QuotaBucket*& quota_bucket) {
  UnirateQuotaBucket* unirate_quota_bucket = new UnirateQuotaBucket();
  ReturnCode ret_code = unirate_quota_bucket->Init(rate_limit_rule);
  if (ret_code == kReturnOk) {
    quota_bucket = unirate_quota_bucket;
  }
  return ret_code;
}

}  // namespace polaris
