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

#ifndef POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_CLIMB_CALL_METRIC_H_
#define POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_CLIMB_CALL_METRIC_H_

#include <stdint.h>
#include <iosfwd>
#include <vector>

#include "polaris/limit.h"
#include "sync/atomic.h"

namespace v1 {
class MetricRequest;
}

namespace polaris {

struct ClimbMetricConfig;
struct ClimbTriggerPolicy;

struct MetricBucket {
  MetricBucket();
  ~MetricBucket();

  void Init(std::size_t size);

  void Increment(std::size_t index);

  uint32_t GetAndClear(std::size_t index);

  std::size_t Size() { return size_; }

private:
  std::size_t size_;
  sync::Atomic<uint32_t>** bucket_;
};

// 接口调用数据
class CallMetricData {
public:
  CallMetricData(ClimbMetricConfig& metric_config, ClimbTriggerPolicy& trigger_policy);
  ~CallMetricData();

  void Record(LimitCallResultType result_type, uint64_t response_time, int response_code);

  void Serialize(v1::MetricRequest* metric_request);

private:
  ClimbMetricConfig& metric_config_;
  ClimbTriggerPolicy& trigger_policy_;
  uint64_t bucket_time_;
  std::vector<MetricBucket> metric_data_;
  uint64_t last_serialize_time_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_CLIMB_CALL_METRIC_H_
