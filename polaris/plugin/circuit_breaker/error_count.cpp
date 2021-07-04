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

#include "plugin/circuit_breaker/error_count.h"

#include <utility>

#include "plugin/circuit_breaker/circuit_breaker.h"
#include "polaris/config.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

ErrorCountCircuitBreaker::ErrorCountCircuitBreaker() {
  continue_error_threshold_         = 0;
  sleep_window_                     = 0;
  request_count_after_half_open_    = 0;
  success_count_half_open_to_close_ = 0;
  error_count_half_open_to_open_    = 0;
  metric_expired_time_              = 0;
  pthread_rwlock_init(&rwlock_, 0);
}

ErrorCountCircuitBreaker::~ErrorCountCircuitBreaker() {
  pthread_rwlock_destroy(&rwlock_);
  error_count_map_.clear();
}

ReturnCode ErrorCountCircuitBreaker::Init(Config* config, Context* /*context*/) {
  continue_error_threshold_ =
      config->GetIntOrDefault(CircuitBreakerConfig::kContinuousErrorThresholdKey,
                              CircuitBreakerConfig::kContinuousErrorThresholdDefault);
  sleep_window_ = config->GetMsOrDefault(CircuitBreakerConfig::kHalfOpenSleepWindowKey,
                                         CircuitBreakerConfig::kHalfOpenSleepWindowDefault);
  request_count_after_half_open_ =
      config->GetIntOrDefault(CircuitBreakerConfig::kRequestCountAfterHalfOpenKey,
                              CircuitBreakerConfig::kRequestCountAfterHalfOpenDefault);
  success_count_half_open_to_close_ =
      config->GetIntOrDefault(CircuitBreakerConfig::kSuccessCountAfterHalfOpenKey,
                              CircuitBreakerConfig::kSuccessCountAfterHalfOpenDefault);
  metric_expired_time_ = config->GetIntOrDefault(CircuitBreakerConfig::kMetricExpiredTimeKey,
                                                 CircuitBreakerConfig::kMetricExpiredTimeDefault);

  // 校验配置有效性
  if (continue_error_threshold_ <= 0) {
    continue_error_threshold_ = CircuitBreakerConfig::kContinuousErrorThresholdDefault;
  }
  if (sleep_window_ <= 0) {
    sleep_window_ = CircuitBreakerConfig::kHalfOpenSleepWindowDefault;
  }
  if (request_count_after_half_open_ <= 0) {
    request_count_after_half_open_ = CircuitBreakerConfig::kRequestCountAfterHalfOpenDefault;
  }
  if (success_count_half_open_to_close_ <= 0) {
    success_count_half_open_to_close_ = CircuitBreakerConfig::kSuccessCountAfterHalfOpenDefault;
  } else if (success_count_half_open_to_close_ > request_count_after_half_open_) {
    success_count_half_open_to_close_ = request_count_after_half_open_;
  }
  error_count_half_open_to_open_ =
      request_count_after_half_open_ - success_count_half_open_to_close_ + 1;
  if (metric_expired_time_ <= 0) {
    metric_expired_time_ = CircuitBreakerConfig::kMetricExpiredTimeDefault;
  }
  return kReturnOk;
}

ReturnCode ErrorCountCircuitBreaker::RealTimeCircuitBreak(
    const InstanceGauge& instance_gauge, InstancesCircuitBreakerStatus* instances_status) {
  uint64_t current_time = Time::GetCurrentTimeMs();
  ErrorCountStatus& error_count_status =
      GetOrCreateErrorCountStatus(instance_gauge.instance_id, current_time);
  if (instance_gauge.call_ret_status != kCallRetOk) {
    // 正常状态下
    if (error_count_status.status == kCircuitBreakerClose) {
      ATOMIC_INC(&error_count_status.error_count);
      if (error_count_status.error_count >= continue_error_threshold_) {  // 达到熔断条件
        if (instances_status->TranslateStatus(instance_gauge.instance_id, kCircuitBreakerClose,
                                              kCircuitBreakerOpen)) {
          error_count_status.status           = kCircuitBreakerOpen;
          error_count_status.last_update_time = current_time;
        }
      }
    } else if (error_count_status.status == kCircuitBreakerHalfOpen) {
      ATOMIC_INC(&error_count_status.error_count);
      // 半开状态下的探测请求只要有一个错误则立刻熔断
      // 在请求量较少的时候可使半开后快速又进入熔断状态，避免半开探测占比过高
      if (error_count_status.error_count >= error_count_half_open_to_open_) {
        if (instances_status->TranslateStatus(instance_gauge.instance_id, kCircuitBreakerHalfOpen,
                                              kCircuitBreakerOpen)) {
          error_count_status.status           = kCircuitBreakerOpen;
          error_count_status.last_update_time = current_time;
        }
      }
    }
  } else {
    if (error_count_status.status == kCircuitBreakerHalfOpen) {
      ATOMIC_INC(&error_count_status.success_count);
    } else {
      error_count_status.error_count = 0;
    }
  }
  return kReturnOk;
}

ReturnCode ErrorCountCircuitBreaker::TimingCircuitBreak(
    InstancesCircuitBreakerStatus* instances_status) {
  uint64_t current_time = Time::GetCurrentTimeMs();
  std::map<std::string, ErrorCountStatus>::iterator it;
  pthread_rwlock_rdlock(&rwlock_);
  for (it = error_count_map_.begin(); it != error_count_map_.end(); ++it) {
    ErrorCountStatus& error_count_status = it->second;
    if (error_count_status.status == kCircuitBreakerOpen) {  // 熔断状态
      // 达到半开条件
      if (instances_status->AutoHalfOpenEnable() &&
          error_count_status.last_update_time + sleep_window_ <= current_time) {
        if (instances_status->TranslateStatus(it->first, kCircuitBreakerOpen,
                                              kCircuitBreakerHalfOpen)) {
          error_count_status.status           = kCircuitBreakerHalfOpen;
          error_count_status.success_count    = 0;
          error_count_status.error_count      = 0;
          error_count_status.last_update_time = current_time;
        }
      }
    } else if (error_count_status.status == kCircuitBreakerHalfOpen) {              // 半开状态
      if (error_count_status.success_count >= success_count_half_open_to_close_) {  // 达到恢复条件
        if (instances_status->TranslateStatus(it->first, kCircuitBreakerHalfOpen,
                                              kCircuitBreakerClose)) {
          error_count_status.status           = kCircuitBreakerClose;
          error_count_status.error_count      = 0;
          error_count_status.last_update_time = current_time;
        }
      } else if (error_count_status.last_update_time + 20 * sleep_window_ <= current_time) {
        // 兜底：如果访问量一定时间达不到要求，则重新熔断
        if (instances_status->TranslateStatus(it->first, kCircuitBreakerHalfOpen,
                                              kCircuitBreakerOpen)) {
          error_count_status.status           = kCircuitBreakerOpen;
          error_count_status.last_update_time = current_time;
        }
      }
    }
    // 正常状态不做处理
  }
  pthread_rwlock_unlock(&rwlock_);
  CheckAndExpiredMetric(instances_status);
  return kReturnOk;
}

ErrorCountStatus& ErrorCountCircuitBreaker::GetOrCreateErrorCountStatus(
    const std::string& instance_id, uint64_t current_time) {
  pthread_rwlock_rdlock(&rwlock_);
  std::map<std::string, ErrorCountStatus>::iterator it = error_count_map_.find(instance_id);
  if (it != error_count_map_.end()) {
    it->second.last_access_time = current_time;
    pthread_rwlock_unlock(&rwlock_);
    return it->second;
  }
  pthread_rwlock_unlock(&rwlock_);

  pthread_rwlock_wrlock(&rwlock_);
  it = error_count_map_.find(instance_id);  // double check
  if (it != error_count_map_.end()) {
    it->second.last_access_time = current_time;
    pthread_rwlock_unlock(&rwlock_);
    return it->second;
  }
  ErrorCountStatus& error_count_status = error_count_map_[instance_id];
  error_count_status.status            = kCircuitBreakerClose;
  error_count_status.error_count       = 0;
  error_count_status.success_count     = 0;
  error_count_status.last_update_time  = 0;
  error_count_status.last_access_time  = current_time;
  pthread_rwlock_unlock(&rwlock_);
  return error_count_status;
}

void ErrorCountCircuitBreaker::CheckAndExpiredMetric(
    InstancesCircuitBreakerStatus* instances_status) {
  uint64_t current_time = Time::GetCurrentTimeMs();
  std::map<std::string, ErrorCountStatus>::iterator it;
  pthread_rwlock_wrlock(&rwlock_);
  for (it = error_count_map_.begin(); it != error_count_map_.end();) {
    if (it->second.last_access_time + metric_expired_time_ <= current_time) {
      instances_status->TranslateStatus(it->first, kCircuitBreakerOpen, kCircuitBreakerClose);
      instances_status->TranslateStatus(it->first, kCircuitBreakerHalfOpen, kCircuitBreakerClose);
      error_count_map_.erase(it++);
    } else {
      ++it;
    }
  }
  pthread_rwlock_unlock(&rwlock_);
}

}  // namespace polaris
