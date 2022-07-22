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

#include "quota/adjuster/climb_call_metric.h"

#include <stddef.h>
#include <v1/metric.pb.h>

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "quota/adjuster/climb_config.h"
#include "utils/time_clock.h"

namespace polaris {

MetricBucket::MetricBucket() : size_(0), bucket_(nullptr) {}

MetricBucket::~MetricBucket() {
  for (std::size_t i = 0; i < size_; ++i) {
    if (bucket_[i] != nullptr) {
      delete bucket_[i];
      bucket_[i] = nullptr;
    }
  }
  if (bucket_ != nullptr) {
    delete[] bucket_;
  }
}

void MetricBucket::Init(std::size_t size) {
  size_ = size;
  bucket_ = new std::atomic<uint32_t>*[size_];
  for (std::size_t i = 0; i < size_; ++i) {
    bucket_[i] = new std::atomic<uint32_t>(0);
  }
}

void MetricBucket::Increment(std::size_t index) { ++(*bucket_[index]); }

uint32_t MetricBucket::GetAndClear(std::size_t index) { return bucket_[index]->exchange(0); }

static const std::size_t MetricTypeReqCount = 0;    // 总请求
static const std::size_t MetricTypeLimitCount = 1;  // 限流数
static const std::size_t MetricTypeSlowCount = 2;   // 慢调用数
static const std::size_t MetricTypeErrorCount = 3;  // 错误数
static const std::size_t MetricTypeBaseLength = 4;  // 基础类型种类

CallMetricData::CallMetricData(ClimbMetricConfig& metric_config, ClimbTriggerPolicy& trigger_policy)
    : metric_config_(metric_config), trigger_policy_(trigger_policy) {
  // 默认窗口长度60s，精度100，则每个bucket时长600ms
  bucket_time_ = metric_config_.window_size_ / metric_config_.precision_;
  // 默认每20s上报一次，保留20*1000/600+1，额外保留2s窗口防止数据丢失
  metric_data_.resize(metric_config_.report_interval_ / bucket_time_ + 1 + 2000 / bucket_time_);
  for (std::size_t i = 0; i < metric_data_.size(); ++i) {
    // 支持总请求数、限流数、错误数、慢调用数 + 特殊错误码
    metric_data_[i].Init(MetricTypeBaseLength + trigger_policy.error_specials_.size());
  }
  last_serialize_time_ = Time::GetSystemTimeMs();
}

CallMetricData::~CallMetricData() {}

void CallMetricData::Record(LimitCallResultType result_type, uint64_t response_time, int response_code) {
  uint64_t current_time = Time::GetSystemTimeMs();
  uint64_t bucket_index = (current_time / bucket_time_) % metric_data_.size();
  MetricBucket& bucket = metric_data_[bucket_index];
  bucket.Increment(MetricTypeReqCount);
  if (result_type == kLimitCallResultLimited) {
    bucket.Increment(MetricTypeLimitCount);
  } else if (result_type == kLimitCallResultOk) {
    if (response_time >= trigger_policy_.slow_rate_.max_rt_) {
      bucket.Increment(MetricTypeSlowCount);
    }
  } else if (result_type == kLimitCallResultFailed) {
    std::size_t metric_index = MetricTypeErrorCount;
    for (ErrorSpecialPolicies::iterator it = trigger_policy_.error_specials_.begin();
         it != trigger_policy_.error_specials_.end(); ++it) {
      ++metric_index;
      if (it->second.error_codes_.count(response_code) > 0) {
        bucket.Increment(metric_index);
        return;
      }
    }
    bucket.Increment(MetricTypeErrorCount);
  }
}

void CallMetricData::Serialize(v1::MetricRequest* metric_request) {
  uint64_t current_time = Time::GetSystemTimeMs();
  if (current_time < last_serialize_time_) {
    return;  // 时间跳变了
  }
  v1::MetricRequest::MetricIncrement* increment = metric_request->add_increments();
  increment->set_duration(metric_config_.window_size_);
  increment->set_precision(metric_config_.precision_);
  ErrorSpecialPolicies::iterator error_special_it = trigger_policy_.error_specials_.begin();
  for (std::size_t i = 0; current_time >= last_serialize_time_ && i < metric_data_.size(); ++i) {
    uint64_t bucket_index = (current_time / bucket_time_) % metric_data_.size();
    for (std::size_t type = 0; type < metric_data_[bucket_index].Size(); ++type) {  // 基础类型
      v1::MetricRequest::MetricIncrement::Values* values;
      if (i == 0) {
        values = increment->add_values();
        v1::MetricType metric_type = v1::ReqCount;
        if (type < MetricTypeBaseLength) {
          metric_type = static_cast<v1::MetricType>(type);
          values->mutable_dimension()->set_type(metric_type);
          if (type == MetricTypeSlowCount) {
            values->mutable_dimension()->set_value(std::to_string(trigger_policy_.slow_rate_.max_rt_));
          }
        } else {
          values->mutable_dimension()->set_type(v1::ErrorCountByType);
          values->mutable_dimension()->set_value(error_special_it->first);
          error_special_it++;
        }
      } else {
        values = increment->mutable_values(type);
      }
      values->add_values(metric_data_[bucket_index].GetAndClear(type));
    }
    current_time -= bucket_time_;
  }
  last_serialize_time_ = current_time;
}

}  // namespace polaris
