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

#include "context/context_impl.h"
#include "model/constants.h"
#include "plugin/circuit_breaker/chain.h"
#include "polaris/config.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

ErrorCountCircuitBreaker::ErrorCountCircuitBreaker()
    : context_(nullptr),
      continue_error_threshold_(0),
      request_count_after_half_open_(0),
      sleep_window_(0),
      success_count_half_open_to_close_(0),
      error_count_half_open_to_open_(0),
      metric_expired_time_(0) {}

ErrorCountCircuitBreaker::~ErrorCountCircuitBreaker() { context_ = nullptr; }

ReturnCode ErrorCountCircuitBreaker::Init(Config* config, Context* context) {
  context_ = context;
  continue_error_threshold_ =
      config->GetIntOrDefault(constants::kContinuousErrorThresholdKey, constants::kContinuousErrorThresholdDefault);
  sleep_window_ = config->GetMsOrDefault(constants::kHalfOpenSleepWindowKey, constants::kHalfOpenSleepWindowDefault);
  request_count_after_half_open_ =
      config->GetIntOrDefault(constants::kRequestCountAfterHalfOpenKey, constants::kRequestCountAfterHalfOpenDefault);
  success_count_half_open_to_close_ =
      config->GetIntOrDefault(constants::kSuccessCountAfterHalfOpenKey, constants::kSuccessCountAfterHalfOpenDefault);
  metric_expired_time_ =
      config->GetIntOrDefault(constants::kMetricExpiredTimeKey, constants::kMetricExpiredTimeDefault);

  // 校验配置有效性
  if (continue_error_threshold_ <= 0) {
    continue_error_threshold_ = constants::kContinuousErrorThresholdDefault;
  }
  if (sleep_window_ <= 0) {
    sleep_window_ = constants::kHalfOpenSleepWindowDefault;
  }
  if (request_count_after_half_open_ <= 0) {
    request_count_after_half_open_ = constants::kRequestCountAfterHalfOpenDefault;
  }
  if (success_count_half_open_to_close_ <= 0) {
    success_count_half_open_to_close_ = constants::kSuccessCountAfterHalfOpenDefault;
  } else if (success_count_half_open_to_close_ > request_count_after_half_open_) {
    success_count_half_open_to_close_ = request_count_after_half_open_;
  }
  error_count_half_open_to_open_ = request_count_after_half_open_ - success_count_half_open_to_close_ + 1;
  if (metric_expired_time_ <= 0) {
    metric_expired_time_ = constants::kMetricExpiredTimeDefault;
  }
  return kReturnOk;
}

ReturnCode ErrorCountCircuitBreaker::RealTimeCircuitBreak(const InstanceGauge& instance_gauge,
                                                          InstancesCircuitBreakerStatus* instances_status) {
  ErrorCountStatus* error_count_status = GetOrCreateErrorCountStatus(instance_gauge.instance_id);
  if (instance_gauge.call_ret_status != kCallRetOk) {
    // 正常状态下
    if (error_count_status->status == kCircuitBreakerClose) {
      int error_count = error_count_status->error_count.fetch_add(1, std::memory_order_relaxed);
      if (error_count + 1 >= continue_error_threshold_) {  // 达到熔断条件
        if (instances_status->TranslateStatus(instance_gauge.instance_id, kCircuitBreakerClose, kCircuitBreakerOpen)) {
          error_count_status->status = kCircuitBreakerOpen;
          error_count_status->last_update_time = Time::GetCoarseSteadyTimeMs();
        }
      }
    } else if (error_count_status->status == kCircuitBreakerHalfOpen) {
      int error_count = error_count_status->error_count.fetch_add(1, std::memory_order_relaxed);
      // 半开状态下的探测请求只要有一个错误则立刻熔断
      // 在请求量较少的时候可使半开后快速又进入熔断状态，避免半开探测占比过高
      if (error_count + 1 >= error_count_half_open_to_open_) {
        if (instances_status->TranslateStatus(instance_gauge.instance_id, kCircuitBreakerHalfOpen,
                                              kCircuitBreakerOpen)) {
          error_count_status->status = kCircuitBreakerOpen;
          error_count_status->last_update_time = Time::GetCoarseSteadyTimeMs();
        }
      }
    }
  } else {
    if (error_count_status->status == kCircuitBreakerHalfOpen) {
      error_count_status->success_count.fetch_add(1, std::memory_order_relaxed);
    } else {
      error_count_status->error_count.store(0, std::memory_order_relaxed);
    }
  }
  return kReturnOk;
}

ReturnCode ErrorCountCircuitBreaker::TimingCircuitBreak(InstancesCircuitBreakerStatus* instances_status) {
  std::unordered_map<std::string, std::shared_ptr<ErrorCountStatus>> all_error_count;
  error_count_map_.GetAllData(all_error_count);

  uint64_t current_time = Time::GetCoarseSteadyTimeMs();
  for (auto it : all_error_count) {
    std::shared_ptr<ErrorCountStatus>& error_count_status = it.second;
    if (error_count_status->status == kCircuitBreakerOpen) {  // 熔断状态
      // 达到半开条件
      if (instances_status->AutoHalfOpenEnable() &&
          error_count_status->last_update_time + sleep_window_ <= current_time) {
        if (instances_status->TranslateStatus(it.first, kCircuitBreakerOpen, kCircuitBreakerHalfOpen)) {
          error_count_status->status = kCircuitBreakerHalfOpen;
          error_count_status->success_count.store(0, std::memory_order_relaxed);
          error_count_status->error_count.store(0, std::memory_order_relaxed);
          error_count_status->last_update_time = current_time;
        }
      }
    } else if (error_count_status->status == kCircuitBreakerHalfOpen) {              // 半开状态
      if (error_count_status->success_count >= success_count_half_open_to_close_) {  // 达到恢复条件
        if (instances_status->TranslateStatus(it.first, kCircuitBreakerHalfOpen, kCircuitBreakerClose)) {
          error_count_status->status = kCircuitBreakerClose;
          error_count_status->error_count.store(0, std::memory_order_relaxed);
          error_count_status->last_update_time = current_time;
        }
      } else if (error_count_status->last_update_time + 100 * sleep_window_ <= current_time) {
        // 兜底：如果访问量一定时间达不到要求，则重新熔断
        if (instances_status->TranslateStatus(it.first, kCircuitBreakerHalfOpen, kCircuitBreakerOpen)) {
          error_count_status->status = kCircuitBreakerOpen;
          error_count_status->last_update_time = current_time;
        }
      }
    }
    // 正常状态不做处理
  }
  return kReturnOk;
}

ReturnCode ErrorCountCircuitBreaker::DetectToHalfOpen(const std::string& instance_id) {
  std::shared_ptr<ErrorCountStatus> error_count_status = error_count_map_.Get(instance_id);
  if (error_count_status != nullptr && error_count_status->status == kCircuitBreakerOpen) {
    error_count_status->status = kCircuitBreakerHalfOpen;
    error_count_status->success_count.store(0, std::memory_order_relaxed);
    error_count_status->error_count.store(0, std::memory_order_relaxed);
    error_count_status->last_update_time = Time::GetCoarseSteadyTimeMs();
  }
  return kReturnOk;
}

ErrorCountStatus* ErrorCountCircuitBreaker::GetOrCreateErrorCountStatus(const std::string& instance_id) {
  ErrorCountStatus* status = error_count_map_.GetWithRcuTime(instance_id);
  if (status != nullptr) {
    return status;
  }
  std::shared_ptr<ErrorCountStatus> created_status = error_count_map_.CreateOrGet(
      instance_id, [=] { return std::shared_ptr<ErrorCountStatus>(new ErrorCountStatus()); });
  return created_status.get();
}

void ErrorCountCircuitBreaker::CleanStatus(InstancesCircuitBreakerStatus* instances_status,
                                           InstanceExistChecker& exist_checker) {
  std::vector<std::string> expired_instances;
  error_count_map_.CheckExpired(Time::CoarseSteadyTimeSub(metric_expired_time_), expired_instances);
  if (!expired_instances.empty()) {
    std::vector<std::string> delete_instances;
    for (auto instance_id : expired_instances) {
      if (!exist_checker(instance_id)) {
        delete_instances.push_back(instance_id);
        instances_status->TranslateStatus(instance_id, kCircuitBreakerOpen, kCircuitBreakerClose);
        instances_status->TranslateStatus(instance_id, kCircuitBreakerHalfOpen, kCircuitBreakerClose);
      }
    }
    error_count_map_.Delete(delete_instances);
  }
  uint64_t rcu_min_time = context_->GetContextImpl()->RcuMinTime();
  error_count_map_.CheckGc(rcu_min_time > 1000 ? rcu_min_time - 1000 : 0);
}

}  // namespace polaris
