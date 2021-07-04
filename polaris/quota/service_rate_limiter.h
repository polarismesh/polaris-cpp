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

#ifndef POLARIS_CPP_POLARIS_QUOTA_SERVICE_RATE_LIMITER_H_
#define POLARIS_CPP_POLARIS_QUOTA_SERVICE_RATE_LIMITER_H_

#include <stdint.h>

#include "polaris/defs.h"
#include "polaris/limit.h"
#include "quota/model/rate_limit_rule.h"
#include "sync/atomic.h"

namespace polaris {

// 配额分配结果
struct QuotaResult {
  QuotaResult(QuotaResultCode result_code, uint64_t queue_time)
      : result_code_(result_code), queue_time_(queue_time) {}

  QuotaResultCode result_code_;  // 配额分配返回码
  uint64_t queue_time_;          // 排队时间，标识多长时间后可以有新配额供应
};

// 配额池接口
class QuotaBucket {
public:
  virtual ~QuotaBucket() {}

  //在令牌桶/漏桶中进行单个配额的划扣，并返回本次分配的结果
  virtual QuotaResult* GetQuota(int64_t acquire_amount) = 0;

  //释放配额（仅对于并发数限流有用）
  virtual void Release() = 0;
};

// 服务限流处理插件接口
class ServiceRateLimiter {
public:
  virtual ~ServiceRateLimiter() {}

  // 初始化并创建令牌桶/漏桶。主流程会在首次调用，以及规则对象变更的时候，调用该方法
  virtual ReturnCode InitQuotaBucket(RateLimitRule* rate_limit_rule,
                                     QuotaBucket*& quota_bucket) = 0;

  static ServiceRateLimiter* Create(RateLimitActionType action_type);
};

///////////////////////////////////////////////////////////////////////////////
// Reject模式 直接拒绝

class RejectQuotaBucket : public QuotaBucket {
public:
  RejectQuotaBucket() {}
  virtual ~RejectQuotaBucket() {}

  virtual QuotaResult* GetQuota(int64_t acquire_amount);

  virtual void Release() {}
};

class RejectServiceRateLimiter : public ServiceRateLimiter {
public:
  RejectServiceRateLimiter() {}
  virtual ~RejectServiceRateLimiter() {}

  virtual ReturnCode InitQuotaBucket(RateLimitRule* rate_limit_rule, QuotaBucket*& quota_bucket);
};

///////////////////////////////////////////////////////////////////////////////
// Unirate模式 匀速排队
class UnirateQuotaBucket : public QuotaBucket {
public:
  UnirateQuotaBucket();
  virtual ~UnirateQuotaBucket();

  ReturnCode Init(RateLimitRule* rate_limit_rule);

  virtual QuotaResult* GetQuota(int64_t acquire_amount);

  virtual void Release() {}

private:
  RateLimitRule* rule_;                     // 限流规则
  uint64_t max_queuing_duration_;           // 最长排队时间
  uint32_t effective_amount_;               // 等效配额
  uint64_t effective_duration_;             // 等效时间窗
  sync::Atomic<uint64_t> effective_rate_;   // 为一个实例生成一个配额的平均时间
  sync::Atomic<uint64_t> last_grant_time_;  // 上次分配配额时间
  bool reject_all_;                         //是不是有amount为0
};

class UnirateServiceRateLimiter : public ServiceRateLimiter {
public:
  UnirateServiceRateLimiter() {}
  virtual ~UnirateServiceRateLimiter() {}

  virtual ReturnCode InitQuotaBucket(RateLimitRule* rate_limit_rule, QuotaBucket*& quota_bucket);
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_SERVICE_RATE_LIMITER_H_
