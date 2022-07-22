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

#ifndef POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_CLIMB_HEALTH_METRIC_H_
#define POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_CLIMB_HEALTH_METRIC_H_

#include <google/protobuf/repeated_field.h>
#include <stdint.h>
#include <v1/metric.pb.h>
#include <v1/request.pb.h>

#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace polaris {

struct ClimbThrottling;
struct ClimbTriggerPolicy;
struct RateLimitAmount;

enum ThrottlingStatus {
  kThrottlingTuneDown,  // 向上调整
  kThrottlingTuneUp,    // 向下调整
  kThrottlingKeeping    // 稳定
};

/// @brief 健康度数据
struct HealthMetricData {
  HealthMetricData() : total_count_(0), limit_count_(0), error_count_(0), slow_count_(0) {}
  uint64_t total_count_;
  uint64_t limit_count_;
  uint64_t error_count_;
  uint64_t slow_count_;
  std::map<std::string, uint64_t> specail_count_;
};

class HealthMetricClimb {
 public:
  HealthMetricClimb(ClimbTriggerPolicy& trigger_policy, ClimbThrottling& throttling);
  ~HealthMetricClimb();

  // 更新数据
  void Update(const v1::MetricResponse& response);

  // 尝试调整配额，并返回是否进行了调整
  bool TryAdjust(std::vector<RateLimitAmount>& limit_amounts);

  void CollectRecord(v1::RateLimitRecord& rate_limit_record);

 private:
  bool IsUnhealthy();  // 是否不健康

  bool TuneUp(std::vector<RateLimitAmount>& limit_amounts);

  bool TuneDown(std::vector<RateLimitAmount>& limit_amounts);

  void RecordChange(uint32_t before, RateLimitAmount& amount);

 private:
  ClimbTriggerPolicy& trigger_policy_;
  ClimbThrottling& throttling_;
  HealthMetricData metric_data_;
  ThrottlingStatus status_;
  int trigger_count_;  // 触发次数
  std::mutex changes_lock_;
  std::ostringstream reason_;
  google::protobuf::RepeatedPtrField<v1::ThresholdChange> threshold_changes_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_CLIMB_HEALTH_METRIC_H_
