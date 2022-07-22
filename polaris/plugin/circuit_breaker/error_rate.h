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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_ERROR_RATE_H_
#define POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_ERROR_RATE_H_

#include <stdint.h>

#include <atomic>
#include <string>

#include "cache/rcu_unordered_map.h"
#include "plugin/circuit_breaker/circuit_breaker.h"
#include "polaris/defs.h"

namespace polaris {

struct ErrorRateBucket {
  ErrorRateBucket() : total_count(0), error_count(0), bucket_time(0) {}
  std::atomic<int> total_count;
  std::atomic<int> error_count;
  std::atomic<uint64_t> bucket_time;
};

struct ErrorRateStatus {
  ErrorRateStatus()
      : status(kCircuitBreakerClose), buckets(nullptr), last_update_time(0), total_count(0), error_count(0) {}
  ~ErrorRateStatus() { delete[] buckets; }

  CircuitBreakerStatus status;
  ErrorRateBucket* buckets;
  std::atomic<uint64_t> last_update_time;
  std::atomic<uint64_t> total_count;
  std::atomic<uint64_t> error_count;

  void ClearBuckets(int buckets_num) {
    for (int i = 0; i < buckets_num; i++) {
      buckets[i].bucket_time.store(0, std::memory_order_relaxed);
    }
  }

  void BucketsCount(int buckets_num, uint64_t last_end_bucket_time, int& total_req, int& err_req) {
    for (int i = 0; i < buckets_num; i++) {
      // 跳过上一轮写入的数据
      if (buckets[i].bucket_time.load(std::memory_order_relaxed) > last_end_bucket_time) {
        total_req += buckets[i].total_count.load(std::memory_order_relaxed);
        err_req += buckets[i].error_count.load(std::memory_order_relaxed);
      }
    }
  }
};

class ErrorRateCircuitBreaker : public CircuitBreaker {
 public:
  ErrorRateCircuitBreaker();

  ~ErrorRateCircuitBreaker();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual int RequestAfterHalfOpen() { return request_count_after_half_open_; }

  virtual ReturnCode DetectToHalfOpen(const std::string& instance_id);

  virtual ReturnCode RealTimeCircuitBreak(const InstanceGauge& instance_gauge,
                                          InstancesCircuitBreakerStatus* instances_status);

  virtual ReturnCode TimingCircuitBreak(InstancesCircuitBreakerStatus* instances_status);

  virtual void CleanStatus(InstancesCircuitBreakerStatus* instances_status, InstanceExistChecker& exist_checker);

  ErrorRateStatus* GetOrCreateErrorRateStatus(const std::string& instance_id);

 private:
  Context* context_;
  int request_volume_threshold_;  // 计算错误率至少需要多少请求
  float error_rate_threshold_;
  uint64_t metric_stat_time_window_;
  int metric_num_buckets_;
  int metric_bucket_time_;
  uint64_t sleep_window_;
  int request_count_after_half_open_;  // 半开后释放多少个请求
  int success_count_after_half_open_;  // 半开后请求成功多少次恢复

  uint64_t metric_expired_time_;

  RcuUnorderedMap<std::string, ErrorRateStatus> error_rate_map_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_ERROR_RATE_H_
