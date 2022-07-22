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

#include "plugin/circuit_breaker/error_rate.h"

#include <math.h>

#include "context/context_impl.h"
#include "model/constants.h"
#include "plugin/circuit_breaker/chain.h"
#include "polaris/config.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

ErrorRateCircuitBreaker::ErrorRateCircuitBreaker()
    : context_(nullptr),
      request_volume_threshold_(0),
      error_rate_threshold_(0),
      metric_stat_time_window_(0),
      metric_num_buckets_(0),
      metric_bucket_time_(0),
      sleep_window_(0),
      request_count_after_half_open_(0),
      success_count_after_half_open_(0),
      metric_expired_time_(0) {}

ErrorRateCircuitBreaker::~ErrorRateCircuitBreaker() { context_ = nullptr; }

ReturnCode ErrorRateCircuitBreaker::Init(Config* config, Context* context) {
  context_ = context;
  request_volume_threshold_ =
      config->GetIntOrDefault(constants::kRequestVolumeThresholdKey, constants::kRequestVolumeThresholdDefault);
  error_rate_threshold_ =
      config->GetFloatOrDefault(constants::kErrorRateThresholdKey, constants::kErrorRateThresholdDefault);
  metric_stat_time_window_ =
      config->GetMsOrDefault(constants::kMetricStatTimeWindowKey, constants::kMetricStatTimeWindowDefault);
  metric_num_buckets_ = config->GetIntOrDefault(constants::kMetricNumBucketsKey, constants::kMetricNumBucketsDefault);
  sleep_window_ = config->GetMsOrDefault(constants::kHalfOpenSleepWindowKey, constants::kHalfOpenSleepWindowDefault);
  request_count_after_half_open_ =
      config->GetIntOrDefault(constants::kRequestCountAfterHalfOpenKey, constants::kRequestCountAfterHalfOpenDefault);
  success_count_after_half_open_ =
      config->GetIntOrDefault(constants::kSuccessCountAfterHalfOpenKey, constants::kSuccessCountAfterHalfOpenDefault);
  metric_expired_time_ = config->GetMsOrDefault(constants::kMetricExpiredTimeKey, constants::kMetricExpiredTimeDefault);

  // 校验配置有效性
  if (request_volume_threshold_ <= 0) {
    request_volume_threshold_ = constants::kRequestVolumeThresholdDefault;
  }
  if (error_rate_threshold_ <= 0 || error_rate_threshold_ >= 1) {
    error_rate_threshold_ = constants::kErrorRateThresholdDefault;
  }
  if (metric_stat_time_window_ <= 0) {
    metric_stat_time_window_ = constants::kMetricStatTimeWindowDefault;
  }
  if (metric_num_buckets_ <= 0) {
    metric_num_buckets_ = constants::kMetricNumBucketsDefault;
  }
  metric_bucket_time_ = static_cast<int>(ceilf(static_cast<float>(metric_stat_time_window_) / metric_num_buckets_));
  if (sleep_window_ <= 0) {
    sleep_window_ = constants::kHalfOpenSleepWindowDefault;
  }
  if (request_count_after_half_open_ <= 0) {
    request_count_after_half_open_ = constants::kRequestCountAfterHalfOpenDefault;
  }
  if (success_count_after_half_open_ <= 0) {
    success_count_after_half_open_ = constants::kSuccessCountAfterHalfOpenDefault;
  } else if (success_count_after_half_open_ > request_count_after_half_open_) {
    success_count_after_half_open_ = request_count_after_half_open_;
  }
  if (metric_expired_time_ <= 0) {
    metric_expired_time_ = constants::kMetricExpiredTimeDefault;
  }
  return kReturnOk;
}

ReturnCode ErrorRateCircuitBreaker::RealTimeCircuitBreak(const InstanceGauge& instance_gauge,
                                                         InstancesCircuitBreakerStatus* /*instances_status*/) {
  // 错误率熔断使用定时接口进行熔断状态改变，实时接口只统计
  ErrorRateStatus* error_rate_status = GetOrCreateErrorRateStatus(instance_gauge.instance_id);

  if (error_rate_status->status == kCircuitBreakerHalfOpen) {  // 半开状态
    error_rate_status->total_count.fetch_add(1, std::memory_order_relaxed);
    if (instance_gauge.call_ret_status != kCallRetOk) {
      error_rate_status->error_count.fetch_add(1, std::memory_order_relaxed);
    }
    return kReturnOk;
  }

  uint64_t current_time = Time::GetCoarseSteadyTimeMs();
  uint64_t bucket_time = current_time / metric_bucket_time_;
  int bucket_index = bucket_time % metric_num_buckets_;
  // 判断是不是还是上一轮的数据，是的话清空掉
  uint64_t store_bucket_time = error_rate_status->buckets[bucket_index].bucket_time;
  if (bucket_time != store_bucket_time) {
    if (error_rate_status->buckets[bucket_index].bucket_time.compare_exchange_weak(store_bucket_time, bucket_time,
                                                                                   std::memory_order_relaxed)) {
      error_rate_status->buckets[bucket_index].total_count.store(0, std::memory_order_relaxed);
      error_rate_status->buckets[bucket_index].error_count.store(0, std::memory_order_relaxed);
    }
  }
  error_rate_status->buckets[bucket_index].total_count.fetch_add(1, std::memory_order_relaxed);
  if (instance_gauge.call_ret_status != kCallRetOk) {
    error_rate_status->buckets[bucket_index].error_count.fetch_add(1, std::memory_order_relaxed);
  }
  return kReturnOk;
}

ReturnCode ErrorRateCircuitBreaker::TimingCircuitBreak(InstancesCircuitBreakerStatus* instances_status) {
  std::unordered_map<std::string, std::shared_ptr<ErrorRateStatus>> all_error_rate;
  error_rate_map_.GetAllData(all_error_rate);
  uint64_t current_time = Time::GetCoarseSteadyTimeMs();
  uint64_t last_end_bucket_time = current_time / metric_bucket_time_ - metric_num_buckets_;
  for (auto it : all_error_rate) {
    std::shared_ptr<ErrorRateStatus>& error_rate_status = it.second;
    // 熔断状态
    if (error_rate_status->status == kCircuitBreakerOpen) {
      if (instances_status->AutoHalfOpenEnable() &&
          error_rate_status->last_update_time + sleep_window_ <= current_time &&
          instances_status->TranslateStatus(it.first, kCircuitBreakerOpen, kCircuitBreakerHalfOpen)) {
        error_rate_status->last_update_time = current_time;
        error_rate_status->status = kCircuitBreakerHalfOpen;
        error_rate_status->total_count.store(0, std::memory_order_relaxed);
        error_rate_status->error_count.store(0, std::memory_order_relaxed);
        error_rate_status->ClearBuckets(metric_num_buckets_);
      }
      continue;
    }

    if (error_rate_status->status == kCircuitBreakerClose) {
      // 计算数据
      int total_req = 0, err_req = 0;
      error_rate_status->BucketsCount(metric_num_buckets_, last_end_bucket_time, total_req, err_req);
      // 达到熔断条件：请求数达标且错误率达标，request_volume_threshold_大于0能确保total_req大于0才作为除数
      if (total_req >= request_volume_threshold_ &&
          (static_cast<float>(err_req) / total_req >= error_rate_threshold_) &&
          instances_status->TranslateStatus(it.first, kCircuitBreakerClose, kCircuitBreakerOpen)) {
        error_rate_status->last_update_time = current_time;
        error_rate_status->status = kCircuitBreakerOpen;
        // 熔断后不会使用数据判断是否进入半开，这里不用清空数据
      }
      continue;
    }
    // 半开状态
    if (error_rate_status->status == kCircuitBreakerHalfOpen) {
      // 达到恢复条件
      int total_req = error_rate_status->total_count.load(std::memory_order_relaxed);
      int err_req = error_rate_status->error_count.load(std::memory_order_relaxed);
      if (total_req - err_req >= success_count_after_half_open_) {
        if (instances_status->TranslateStatus(it.first, kCircuitBreakerHalfOpen, kCircuitBreakerClose)) {
          error_rate_status->last_update_time = current_time;
          error_rate_status->status = kCircuitBreakerClose;
          error_rate_status->ClearBuckets(metric_num_buckets_);
        }
      } else if (err_req > request_count_after_half_open_ - success_count_after_half_open_ ||
                 error_rate_status->last_update_time + 100 * sleep_window_ <= current_time) {
        // 达到重新熔断条件
        if (instances_status->TranslateStatus(it.first, kCircuitBreakerHalfOpen, kCircuitBreakerOpen)) {
          error_rate_status->last_update_time = current_time;
          error_rate_status->status = kCircuitBreakerOpen;
          error_rate_status->ClearBuckets(metric_num_buckets_);
        }
      }
    }
  }
  return kReturnOk;
}

ReturnCode ErrorRateCircuitBreaker::DetectToHalfOpen(const std::string& instance_id) {
  std::shared_ptr<ErrorRateStatus> error_rate_status = error_rate_map_.Get(instance_id);
  if (error_rate_status != nullptr && error_rate_status->status == kCircuitBreakerOpen) {
    error_rate_status->status = kCircuitBreakerHalfOpen;
    error_rate_status->last_update_time = Time::GetCoarseSteadyTimeMs();
    error_rate_status->ClearBuckets(metric_num_buckets_);
  }
  return kReturnOk;
}

ErrorRateStatus* ErrorRateCircuitBreaker::GetOrCreateErrorRateStatus(const std::string& instance_id) {
  ErrorRateStatus* status = error_rate_map_.GetWithRcuTime(instance_id);
  if (status != nullptr) {
    return status;
  }
  std::shared_ptr<ErrorRateStatus> created_status = error_rate_map_.CreateOrGet(instance_id, [=] {
    std::shared_ptr<ErrorRateStatus> error_rate_status(new ErrorRateStatus());
    error_rate_status->buckets = new ErrorRateBucket[metric_num_buckets_];
    for (int i = 0; i < metric_num_buckets_; i++) {
      error_rate_status->buckets[i].bucket_time = 0;
    }
    return error_rate_status;
  });
  return created_status.get();
}

void ErrorRateCircuitBreaker::CleanStatus(InstancesCircuitBreakerStatus* instances_status,
                                          InstanceExistChecker& exist_checker) {
  std::vector<std::string> expired_instances;
  error_rate_map_.CheckExpired(Time::CoarseSteadyTimeSub(metric_expired_time_), expired_instances);
  if (!expired_instances.empty()) {
    std::vector<std::string> delete_instances;
    for (auto instance_id : expired_instances) {
      if (!exist_checker(instance_id)) {
        delete_instances.push_back(instance_id);
        instances_status->TranslateStatus(instance_id, kCircuitBreakerOpen, kCircuitBreakerClose);
        instances_status->TranslateStatus(instance_id, kCircuitBreakerHalfOpen, kCircuitBreakerClose);
      }
    }
    error_rate_map_.Delete(delete_instances);
  }
  uint64_t rcu_min_time = context_->GetContextImpl()->RcuMinTime();
  error_rate_map_.CheckGc(rcu_min_time > 1000 ? rcu_min_time - 1000 : 0);
}

}  // namespace polaris
