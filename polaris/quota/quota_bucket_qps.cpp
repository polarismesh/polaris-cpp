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

#include "quota/quota_bucket_qps.h"

#include <inttypes.h>
#include <math.h>
#include <stddef.h>

#include <memory>
#include <utility>
#include <vector>

#include "logger.h"
#include "polaris/limit.h"
#include "quota/model/rate_limit_rule.h"
#include "quota_model.h"
#include "utils/time_clock.h"

namespace polaris {

TokenBucket::TokenBucket()
    : global_max_amount_(0), local_max_amount_(0), bucket_time_(0), bucket_stat_(0),
      pending_bucket_time_(0), pending_bucket_stat_(0) {}

TokenBucket::TokenBucket(const TokenBucket& other) {
  global_max_amount_   = other.global_max_amount_.Load();
  local_max_amount_    = other.local_max_amount_;
  bucket_time_         = other.bucket_time_.Load();
  bucket_stat_         = other.bucket_stat_.Load();
  pending_bucket_time_ = other.pending_bucket_time_;
  pending_bucket_stat_ = other.pending_bucket_stat_;
}

void TokenBucket::Init(const RateLimitAmount& amount, uint64_t current_time,
                       int64_t local_max_amount) {
  global_max_amount_   = amount.max_amount_;
  local_max_amount_    = local_max_amount;
  bucket_time_         = current_time / amount.valid_duration_;
  bucket_stat_         = 0;
  pending_bucket_time_ = bucket_time_;
  pending_bucket_stat_ = 0;
  // 初始化远程配额为本地配额
  remote_quota_.remote_token_total_ = local_max_amount_;
  remote_quota_.remote_token_left_  = local_max_amount_;
}

bool TokenBucket::GetToken(int64_t acquire_amount, uint64_t expect_bucket_time,
                           bool use_remote_quota, int64_t& left_quota) {
  uint64_t current_bucket_time = bucket_time_.Load();  // 当前bucket时间
  if (expect_bucket_time != current_bucket_time &&     // 说明需要重置bucket计数
      bucket_time_.Cas(current_bucket_time, expect_bucket_time)) {
    bucket_stat_         = 0;
    pending_bucket_time_ = current_bucket_time;
    pending_bucket_stat_ = 0;

    // 重置为本地最大配额数，防止网络出问题时出现过度限流的情况
    remote_quota_.remote_token_total_ = local_max_amount_;
    remote_quota_.remote_token_left_  = local_max_amount_;
    remote_quota_.quota_need_sync_    = 0;
  }
  int64_t quota_used = (bucket_stat_ += acquire_amount);  // 先增加配额到本地统计
  if (use_remote_quota) {
    left_quota = (remote_quota_.remote_token_left_ -= acquire_amount);
    if (left_quota < 0) {                              // 配额划扣失败，退出
      remote_quota_.limit_request_ += acquire_amount;  // 记录划扣失败请求数
      return false;
    }
    remote_quota_.quota_need_sync_ += acquire_amount;
  } else {  // 不使用远程配置，则通过本地配置判断是否超限
    left_quota = local_max_amount_ - quota_used;
    if (left_quota < 0) {
      return false;
    }
  }
  return true;
}

void TokenBucket::ReturnToken(int64_t acquire_amount, bool use_remote_quota) {
  bucket_stat_ -= acquire_amount;
  if (use_remote_quota) {
    remote_quota_.remote_token_left_ += acquire_amount;
  }
}

uint64_t TokenBucket::RefreshToken(int64_t remote_left, int64_t ack_quota,
                                   uint64_t current_bucket_time, bool remote_quota_expired,
                                   uint64_t current_time) {
  int64_t last_token_remote_total   = remote_quota_.remote_token_total_;
  remote_quota_.remote_token_total_ = remote_left;
  uint64_t next_report_time         = Time::kMaxTime;
  if (remote_quota_expired) {  // 初始化或配额已经过期
    int64_t remote_token_left  = remote_quota_.remote_token_left_;
    int64_t remote_token_total = remote_quota_.remote_token_total_;
    while (!remote_quota_.remote_token_left_.Cas(remote_token_left, remote_token_total)) {
      remote_token_total = remote_quota_.remote_token_total_;
      remote_token_left  = remote_quota_.remote_token_left_;
    }
    POLARIS_LOG(LOG_TRACE, "qps bucket reset %" PRId64 "", remote_quota_.remote_token_left_.Load());
  } else {  // 扣除上报过程中分配的配额
    int64_t old_remote_token_left   = 0;
    int64_t new_remote_token_left   = 0;
    int64_t remote_token_total      = 0;
    int64_t quota_used_when_acquire = 0;
    do {
      old_remote_token_left   = remote_quota_.remote_token_left_;
      quota_used_when_acquire = last_token_remote_total - old_remote_token_left - ack_quota;
      remote_token_total      = remote_quota_.remote_token_total_;
      new_remote_token_left =
          remote_token_total - (quota_used_when_acquire > 0 ? quota_used_when_acquire : 0);
    } while (!remote_quota_.remote_token_left_.Cas(old_remote_token_left, new_remote_token_left));
    POLARIS_LOG(LOG_TRACE,
                "qps bucket update %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 "",
                remote_token_total, new_remote_token_left, quota_used_when_acquire,
                old_remote_token_left, ack_quota);
    if (remote_left > 0) {
      int64_t remote_used = global_max_amount_ - new_remote_token_left;
      if (remote_used > 0 && new_remote_token_left > 0) {
        int64_t left_time = new_remote_token_left * current_time / remote_used;
        if (left_time < 80) {
          next_report_time = left_time / 2 + 1;
        }
        POLARIS_LOG(LOG_TRACE, "left time: %" PRId64 " report time:%" PRIu64 "", left_time,
                    next_report_time);
      }
    }
  }
  if (pending_bucket_time_ == current_bucket_time) {
    if (pending_bucket_stat_ >= ack_quota) {
      pending_bucket_stat_ -= ack_quota;
    } else {
      POLARIS_LOG(LOG_TRACE, "qps bucket ack pending expired: %" PRId64 " %" PRId64 "",
                  pending_bucket_stat_, ack_quota);
    }
  } else {
    pending_bucket_stat_ = 0;
    pending_bucket_time_ = current_bucket_time;
  }
  return next_report_time;
}

void TokenBucket::PreparePendingQuota(uint64_t pending_bucket_time, QuotaUsage& quota_usage) {
  if (bucket_time_ == pending_bucket_time) {
    quota_usage.quota_allocated_ = remote_quota_.quota_need_sync_;
    while (!remote_quota_.quota_need_sync_.Cas(quota_usage.quota_allocated_, 0)) {
      quota_usage.quota_allocated_ = remote_quota_.quota_need_sync_;
    }
    quota_usage.quota_rejected_ = remote_quota_.limit_request_;
    while (!remote_quota_.limit_request_.Cas(quota_usage.quota_rejected_, 0)) {
      quota_usage.quota_rejected_ = remote_quota_.limit_request_;
    }
  }
  if (pending_bucket_time_ == pending_bucket_time) {
    pending_bucket_stat_ += quota_usage.quota_allocated_;
  } else {
    pending_bucket_stat_ = quota_usage.quota_allocated_;
    pending_bucket_time_ = pending_bucket_time;
  }
}

void TokenBucket::UpdateLimitAmount(const RateLimitAmount& limit_amount, int64_t local_max_amount) {
  global_max_amount_ = limit_amount.max_amount_;
  local_max_amount_  = local_max_amount;
}

///////////////////////////////////////////////////////////////////////////////

RemoteAwareQpsBucket::RemoteAwareQpsBucket(RateLimitRule* rule) {
  rate_limit_type_ = rule->GetRateLimitType();
  failover_type_   = rule->GetFailoverType();

  uint64_t current_time                       = Time::GetCurrentTimeMs();
  const std::vector<RateLimitAmount>& amounts = rule->GetRateLimitAmount();
  for (std::size_t i = 0; i < amounts.size(); ++i) {
    TokenBucket& bucket = token_buckets_[amounts[i].valid_duration_];
    bucket.Init(amounts[i], current_time, amounts[i].max_amount_);
  }
  POLARIS_ASSERT(!token_buckets_.empty());
  remote_timeout_duration_ = token_buckets_.begin()->first;  // 最小限流周期，远程配额超时
  last_remote_sync_time_.Store(current_time);
}

QuotaResponse* RemoteAwareQpsBucket::Allocate(int64_t acquire_amount, uint64_t current_server_time,
                                              LimitAllocateResult* limit_result) {
  limit_result->max_amount_       = 0;
  limit_result->violate_duration_ = 0;

  uint64_t last_remote_sync_time = last_remote_sync_time_.Load();
  // 全局模式且上报未超时的情况下使用远程配额
  bool remote_not_timeout = current_server_time < last_remote_sync_time + remote_timeout_duration_;
  bool use_remote_quota   = rate_limit_type_ == v1::Rule::GLOBAL && remote_not_timeout;
  limit_result->is_degrade_ = !remote_not_timeout;

  QuotaResultInfo info;
  info.is_degrade_ = limit_result->is_degrade_;
  // 尝试对所有限流配额进行划扣
  std::map<uint64_t, TokenBucket>::iterator violate_bucket_it = token_buckets_.end();
  for (std::map<uint64_t, TokenBucket>::iterator bucket_it = token_buckets_.begin();
       bucket_it != token_buckets_.end(); ++bucket_it) {
    uint64_t expect_bucket_time = current_server_time / bucket_it->first;  // 期望的bucket时间
    if (!bucket_it->second.GetToken(acquire_amount, expect_bucket_time, use_remote_quota,
                                    info.left_quota_)) {
      violate_bucket_it               = bucket_it;
      limit_result->violate_duration_ = bucket_it->first;
      limit_result->max_amount_       = bucket_it->second.GetGlobalMaxAmount();
      // 设置提示信息
      info.left_quota_ = 0;
      info.all_quota_  = bucket_it->second.GetGlobalMaxAmount();
      info.duration_   = bucket_it->first;
      break;
    }
  }
  if (violate_bucket_it == token_buckets_.end()) {  // 配额分配成功
    info.all_quota_ = token_buckets_.rbegin()->second.GetGlobalMaxAmount();
    info.duration_  = token_buckets_.rbegin()->first;
    return QuotaResponseImpl::CreateResponse(kQuotaResultOk, info);
  }
  // 配额分配失败
  violate_bucket_it++;
  for (std::map<uint64_t, TokenBucket>::iterator bucket_it = token_buckets_.begin();
       bucket_it != violate_bucket_it; ++bucket_it) {
    bucket_it->second.ReturnToken(acquire_amount, use_remote_quota);
  }
  if (!use_remote_quota && failover_type_ == v1::Rule::FAILOVER_PASS) {
    return QuotaResponseImpl::CreateResponse(kQuotaResultOk, info);
  } else {
    return QuotaResponseImpl::CreateResponse(kQuotaResultLimited, info);
  }
}

uint64_t RemoteAwareQpsBucket::SetRemoteQuota(const RemoteQuotaResult& remote_quota_result) {
  uint64_t current_time     = remote_quota_result.curret_server_time_;
  uint64_t remote_data_time = remote_quota_result.remote_usage_.create_server_time_;

  QuotaUsageInfo* local_usage = remote_quota_result.local_usage_;
  std::map<uint64_t, TokenBucket>::iterator bucket_it;
  const std::map<uint64_t, QuotaUsage>& remote_usage =
      remote_quota_result.remote_usage_.quota_usage_;
  uint64_t next_report_time = Time::kMaxTime;
  for (std::map<uint64_t, QuotaUsage>::const_iterator it = remote_usage.begin();
       it != remote_usage.end(); ++it) {
    bucket_it = token_buckets_.find(it->first);
    if (bucket_it == token_buckets_.end()) {
      continue;
    }
    TokenBucket& bucket          = bucket_it->second;
    uint64_t current_bucket_time = current_time / it->first;
    int64_t remote_quota         = it->second.quota_allocated_;
    if (remote_data_time / it->first != current_bucket_time) {
      remote_quota = bucket_it->second.GetGlobalMaxAmount();
    }
    int64_t local_used = 0;
    // 非首次且上报前等待确认的数据仍然属于当前计数周期
    if (local_usage != NULL &&
        remote_quota_result.local_usage_->create_server_time_ / it->first == current_bucket_time) {
      local_used = local_usage->quota_usage_[bucket_it->first].quota_allocated_;
    }
    uint64_t report_time = bucket.RefreshToken(remote_quota, local_used, current_bucket_time,
                                               current_time >= last_remote_sync_time_ + it->first,
                                               current_time % it->first);
    if (report_time < next_report_time) {
      next_report_time = report_time;
    }
  }
  last_remote_sync_time_.Store(current_time);
  return next_report_time;
}

QuotaUsageInfo* RemoteAwareQpsBucket::GetQuotaUsage(uint64_t current_server_time) {
  QuotaUsageInfo* result      = new QuotaUsageInfo();
  result->create_server_time_ = current_server_time;
  for (std::map<uint64_t, TokenBucket>::iterator bucket_it = token_buckets_.begin();
       bucket_it != token_buckets_.end(); ++bucket_it) {
    QuotaUsage quota_usage;
    bucket_it->second.PreparePendingQuota(result->create_server_time_ / bucket_it->first,
                                          quota_usage);
    result->quota_usage_[bucket_it->first] = quota_usage;
    POLARIS_LOG(LOG_TRACE, "qps bucket usage %" PRId64 " limit %" PRId64 "",
                quota_usage.quota_allocated_, quota_usage.quota_rejected_);
  }
  return result;
}

void RemoteAwareQpsBucket::UpdateLimitAmount(const std::vector<RateLimitAmount>& amounts) {
  std::map<uint64_t, TokenBucket>::iterator bucket_it;
  for (std::size_t i = 0; i < amounts.size(); ++i) {
    const RateLimitAmount& limit_amount = amounts[i];
    bucket_it                           = token_buckets_.find(limit_amount.valid_duration_);
    POLARIS_ASSERT(bucket_it != token_buckets_.end());
    bucket_it->second.UpdateLimitAmount(limit_amount, limit_amount.max_amount_);
  }
}

}  // namespace polaris
