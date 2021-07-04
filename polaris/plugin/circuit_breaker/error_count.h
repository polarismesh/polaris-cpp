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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_ERROR_COUNT_H_
#define POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_ERROR_COUNT_H_

#include <pthread.h>
#include <stdint.h>

#include <map>
#include <string>

#include "polaris/defs.h"
#include "polaris/plugin.h"

namespace polaris {

class Config;
class Context;

struct ErrorCountStatus {
  CircuitBreakerStatus status;
  int error_count;
  int success_count;
  uint64_t last_update_time;
  uint64_t last_access_time;
};

class ErrorCountCircuitBreaker : public CircuitBreaker {
public:
  ErrorCountCircuitBreaker();

  virtual ~ErrorCountCircuitBreaker();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual int RequestAfterHalfOpen() { return request_count_after_half_open_; }

  virtual ReturnCode RealTimeCircuitBreak(const InstanceGauge& instance_gauge,
                                          InstancesCircuitBreakerStatus* instances_status);

  virtual ReturnCode TimingCircuitBreak(InstancesCircuitBreakerStatus* instances_status);

  ErrorCountStatus& GetOrCreateErrorCountStatus(const std::string& instance_id,
                                                uint64_t current_time);

  void CheckAndExpiredMetric(InstancesCircuitBreakerStatus* instances_status);

private:
  int continue_error_threshold_;  // 连续错误次数熔断阈值
  uint64_t sleep_window_;  // 熔断后等待多久时间转入半开，半开后等待多久未达到恢复条件则重新熔断
  int request_count_after_half_open_;     // 半开后释放多少个请求
  int success_count_half_open_to_close_;  // 半开后请求成功多少次恢复
  int error_count_half_open_to_open_;     // 半开后多少请求失败则立即打开

  uint64_t metric_expired_time_;
  pthread_rwlock_t rwlock_;
  std::map<std::string, ErrorCountStatus> error_count_map_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_ERROR_COUNT_H_
