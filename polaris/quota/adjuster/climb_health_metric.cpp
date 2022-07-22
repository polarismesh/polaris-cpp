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

#include "quota/adjuster/climb_health_metric.h"

#include <math.h>
#include <v1/metric.pb.h>
#include <v1/request.pb.h>

#include <utility>

#include "logger.h"
#include "quota/adjuster/climb_config.h"
#include "quota/model/rate_limit_rule.h"
#include "utils/time_clock.h"

namespace polaris {

HealthMetricClimb::HealthMetricClimb(ClimbTriggerPolicy& trigger_policy, ClimbThrottling& throttling)
    : trigger_policy_(trigger_policy), throttling_(throttling), status_(kThrottlingKeeping), trigger_count_(0) {}

HealthMetricClimb::~HealthMetricClimb() {}

void HealthMetricClimb::Update(const v1::MetricResponse& response) {
  if (response.summaries_size() != 1) {
    POLARIS_LOG(LOG_ERROR, "metric summary data size for climb adjuster not equal 1");
    return;
  }
  HealthMetricData data;
  const v1::MetricResponse::MetricSum& metric_sum = response.summaries(0);
  for (int i = 0; i < metric_sum.values_size(); ++i) {
    const v1::MetricResponse::MetricSum::Value& value = metric_sum.values(i);
    switch (value.dimension().type()) {
      case v1::ReqCount:
        data.total_count_ = value.value();
        break;
      case v1::LimitCount:
        data.limit_count_ = value.value();
        break;
      case v1::ErrorCount:
        data.error_count_ = value.value();
        break;
      case v1::ReqCountByDelay:
        data.slow_count_ = value.value();
        break;
      case v1::ErrorCountByType:
        if (value.value() > 0) {
          data.specail_count_[value.dimension().value()] = value.value();
        }
        break;
      default:
        break;
    }
  }
  metric_data_ = data;
}

bool HealthMetricClimb::IsUnhealthy() {
  uint64_t normal_count =
      metric_data_.total_count_ > metric_data_.limit_count_ ? metric_data_.total_count_ - metric_data_.limit_count_ : 0;
  if (normal_count * trigger_policy_.slow_rate_.slow_rate_ < metric_data_.slow_count_ * 100) {
    reason_ << "slow/normal:" << metric_data_.slow_count_ << "/" << normal_count
            << " > rate:" << trigger_policy_.slow_rate_.slow_rate_ << "%";
    return true;  // 慢调用数达到不健康率
  }
  if (metric_data_.total_count_ > trigger_policy_.error_rate_.request_volume_threshold_) {
    if (normal_count * trigger_policy_.error_rate_.error_rate_ < metric_data_.error_count_ * 100) {
      reason_ << "error/normal:" << metric_data_.error_count_ << "/" << normal_count
              << " > rate:" << trigger_policy_.error_rate_.error_rate_ << "%";
      return true;  // 错误数达到不健康率
    }
    // 特殊错误码
    for (std::map<std::string, uint64_t>::iterator it = metric_data_.specail_count_.begin();
         it != metric_data_.specail_count_.end(); ++it) {
      ErrorSpecialPolicies::iterator special_it = trigger_policy_.error_specials_.find(it->first);
      if (special_it != trigger_policy_.error_specials_.end()) {
        if (normal_count * special_it->second.error_rate_ < it->second * 100) {
          reason_ << special_it->first << " error/normal:" << it->second << "/" << normal_count
                  << " > rate:" << special_it->second.error_rate_ << "%";
          return true;  // 特殊错误率达到不健康率
        }
      }
    }
  }
  return false;
}

bool HealthMetricClimb::TryAdjust(std::vector<RateLimitAmount>& limit_amounts) {
  reason_.str("");      // 重置调整原因
  if (IsUnhealthy()) {  // 不健康，需要触发下调
    if (status_ != kThrottlingTuneDown) {
      status_ = kThrottlingTuneDown;
      trigger_count_ = 0;
    }
    trigger_count_++;
    return TuneDown(limit_amounts);
  } else if (metric_data_.limit_count_ > 0) {  // 健康度正常，且请求被限流
    reason_ << "healthy with limit count:" << metric_data_.limit_count_;
    if (status_ != kThrottlingTuneUp) {
      status_ = kThrottlingTuneUp;
      trigger_count_ = 0;
    }
    return TuneUp(limit_amounts);
  } else {
    status_ = kThrottlingKeeping;
  }
  return false;
}

bool HealthMetricClimb::TuneUp(std::vector<RateLimitAmount>& limit_amounts) {
  bool adjust = false;
  bool limited = metric_data_.limit_count_ * 100 > metric_data_.total_count_ * throttling_.limit_threshold_to_tune_up_;
  uint32_t before_adjust = 0;
  if (limited) {  // 软限以上需要达到一定比例才算被限流
    trigger_count_++;
  }
  for (std::size_t i = 0; i < limit_amounts.size(); ++i) {
    RateLimitAmount& amount = limit_amounts[i];
    if (amount.max_amount_ < amount.start_amount_) {  // 软限以下调整
      before_adjust = amount.max_amount_;
      amount.max_amount_ =
          static_cast<uint32_t>(ceil(amount.max_amount_ * 100.0 / throttling_.cold_below_tune_up_rate_));
      if (amount.max_amount_ > amount.start_amount_) {
        amount.max_amount_ = amount.start_amount_;
      }
      RecordChange(before_adjust, amount);
      adjust = true;
    } else if (amount.max_amount_ < amount.end_amount_ && trigger_count_ >= throttling_.tune_up_period_) {
      before_adjust = amount.max_amount_;
      amount.max_amount_ =
          static_cast<uint32_t>(ceil(amount.max_amount_ * 100.0 / throttling_.cold_above_tune_up_rate_));
      if (amount.max_amount_ > amount.end_amount_) {
        amount.max_amount_ = amount.end_amount_;
      }
      RecordChange(before_adjust, amount);
      adjust = true;
    }
  }
  if (adjust) {
    trigger_count_ = 0;  // 调整之后重置0
  }
  return adjust;
}

bool HealthMetricClimb::TuneDown(std::vector<RateLimitAmount>& limit_amounts) {
  bool adjust = false;
  uint32_t before_adjust = 0;
  for (std::size_t i = 0; i < limit_amounts.size(); ++i) {
    RateLimitAmount& amount = limit_amounts[i];
    if (amount.max_amount_ <= amount.min_amount_) {
      continue;
    } else if (amount.max_amount_ <= amount.start_amount_) {
      before_adjust = amount.max_amount_;
      amount.max_amount_ = amount.max_amount_ * throttling_.cold_below_tune_down_rate_ / 100;
      if (amount.max_amount_ < amount.min_amount_) {
        amount.max_amount_ = amount.min_amount_;
      }
      RecordChange(before_adjust, amount);
      adjust = true;
    } else if (amount.max_amount_ <= amount.end_amount_ && trigger_count_ >= throttling_.tune_down_period_) {
      before_adjust = amount.max_amount_;
      amount.max_amount_ = amount.max_amount_ * throttling_.cold_above_tune_down_rate_ / 100;
      if (amount.max_amount_ < amount.start_amount_) {
        amount.max_amount_ = amount.start_amount_;  // 不要直接降到软限以下
      }
      RecordChange(before_adjust, amount);
      adjust = true;
    }
  }
  if (adjust) {
    trigger_count_ = 0;  // 调整之后重置0
  }
  return adjust;
}

void HealthMetricClimb::RecordChange(uint32_t before, RateLimitAmount& amount) {
  const std::lock_guard<std::mutex> mutex_guard(changes_lock_);
  v1::ThresholdChange* change = threshold_changes_.Add();
  Time::Uint64ToTimestamp(Time::GetSystemTimeMs(), change->mutable_time());
  change->set_oldthreshold(std::to_string(before) + "/" + std::to_string(amount.valid_duration_ / 1000) + "s");
  change->set_newthreshold(std::to_string(amount.max_amount_) + "/" + std::to_string(amount.valid_duration_ / 1000) +
                           "s");
  change->set_reason(reason_.str());
}

void HealthMetricClimb::CollectRecord(v1::RateLimitRecord& rate_limit_record) {
  const std::lock_guard<std::mutex> mutex_guard(changes_lock_);
  threshold_changes_.Swap(rate_limit_record.mutable_threshold_changes());
}

}  // namespace polaris
