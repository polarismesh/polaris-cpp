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

#ifndef POLARIS_CPP_POLARIS_QUOTA_QUOTA_BUCKET_QPS_H_
#define POLARIS_CPP_POLARIS_QUOTA_QUOTA_BUCKET_QPS_H_

#include <stdint.h>
#include <v1/ratelimit.pb.h>

#include <map>
#include <vector>

#include "quota/rate_limit_window.h"
#include "sync/atomic.h"

namespace polaris {

class QuotaResponse;
class RateLimitRule;
struct RateLimitAmount;

struct RemoteQuotaInfo {
  sync::Atomic<int64_t> remote_token_total_;  // 远程下发配额数，不用于划扣
  sync::Atomic<int64_t> remote_token_left_;   // 远程剩余配额，用于划扣
  sync::Atomic<uint64_t> quota_need_sync_;    // 当前bucket需要同步的配额
  sync::Atomic<uint64_t> limit_request_;      // 当前bucket需要同步的限流数
};

// 规则里限流周期内的限流数据
class TokenBucket {
public:
  TokenBucket();

  TokenBucket(const TokenBucket& other);

  // 根据规则配置初始化
  void Init(const RateLimitAmount& amount, uint64_t current_time, int64_t local_max_amount);

  // 分配Token，并返回是否需要立即上报
  bool GetToken(int64_t acquire_amount, uint64_t expect_bucket_time, bool use_remote_quota,
                int64_t& left_quota);

  // 分配失败时归还token
  void ReturnToken(int64_t acquire_amount, bool use_remote_quota);

  // 更新从远程同步的配额信息
  uint64_t RefreshToken(int64_t remote_left, int64_t ack_quota, uint64_t current_bucket_time,
                        bool remote_quota_expired, uint64_t current_time);

  // 更新正在上报的配额信息
  void PreparePendingQuota(uint64_t pending_bucket_time, QuotaUsage& quota_usage);

  // 规则中配置的每个周期分配配额总量
  int64_t GetGlobalMaxAmount() const { return global_max_amount_; }

  void UpdateLocalMaxAmount(int64_t local_max_amount) { local_max_amount_ = local_max_amount; }

  // 更新配额限额
  void UpdateLimitAmount(const RateLimitAmount& limit_amount, int64_t local_max_amount);

private:
  sync::Atomic<int64_t> global_max_amount_;  // 规则中配置的周期分配配额总量
  int64_t local_max_amount_;  // 离线分配时配额数，local模式等于global_max_amount_
                              // global模式等于global_max_amount_/实例数
  sync::Atomic<uint64_t> bucket_time_;  // 当前bucket的时间
  sync::Atomic<int64_t> bucket_stat_;   // 当前bucket计数
  uint64_t pending_bucket_time_;        // 正在上报未应答的配额bucket时间
  int64_t pending_bucket_stat_;         // 正在上报未应答的配额bucket计数
  RemoteQuotaInfo remote_quota_;        // 远程配额信息
};

// 记录本地配额使用信息和服务器同步的配额信息
class RemoteAwareQpsBucket : public RemoteAwareBucket {
public:
  explicit RemoteAwareQpsBucket(RateLimitRule* rule);

  // 分配配额
  virtual QuotaResponse* Allocate(int64_t acquire_amount, uint64_t current_server_time,
                                  LimitAllocateResult* limit_result);

  virtual void Release() {}  // 回收配额

  virtual uint64_t SetRemoteQuota(const RemoteQuotaResult& remote_quota_result);  // 设置远程配额

  virtual QuotaUsageInfo* GetQuotaUsage(uint64_t current_server_time);  // 获取配额使用信息

  // 更新配额信息
  virtual void UpdateLimitAmount(const std::vector<RateLimitAmount>& amounts);

private:
  v1::Rule::Type rate_limit_type_;
  v1::Rule::FailoverType failover_type_;
  uint64_t remote_timeout_duration_;  // 最小周期，上报应答超过1个最小周期未返回，则认为超时

  std::map<uint64_t, TokenBucket> token_buckets_;  // 限流数据，按限流周期索引
  sync::Atomic<uint64_t> last_remote_sync_time_;   // 上一次同步远程配额时间
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_QUOTA_BUCKET_QPS_H_
