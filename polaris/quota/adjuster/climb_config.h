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

#ifndef POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_CLIMB_CONFIG_H_
#define POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_CLIMB_CONFIG_H_

#include <stdint.h>

#include <map>
#include <set>
#include <string>

#include <v1/ratelimit.pb.h>

namespace polaris {

struct ClimbMetricConfig {
  ClimbMetricConfig() : window_size_(0), precision_(0), report_interval_(0) {}
  uint64_t window_size_;  // 限流数据度量周期，默认60s
  uint32_t precision_;  // 数据统计精度，决定数据度量的最小周期，度量滑窗的步长=window/precision
  uint64_t report_interval_;  // 上报周期，默认20s

  void InitMetricConfig(const v1::ClimbConfig::MetricConfig& metric_config);
};

struct ErrorRatePolicy {
  ErrorRatePolicy() : enable_(false), request_volume_threshold_(0), error_rate_(0) {}
  bool enable_;                        // 是否开启
  uint32_t request_volume_threshold_;  // 触发限流调整的最小的请求数
  int32_t error_rate_;                 // 触发限流的错误率配置
};

struct SlowRatePolicy {
  SlowRatePolicy() : enable_(false), max_rt_(0), slow_rate_(0) {}
  bool enable_;        // 是否开启
  uint64_t max_rt_;    // 最大响应时间，超过该响应时间属于慢调用
  int32_t slow_rate_;  // 慢请求率阈值，达到该阈值进行限流
};

struct ErrorSpecialPolicy {
  std::set<int64_t> error_codes_;  // 特定规则针对的错误码
  int32_t error_rate_;             //特定规则错误率
};
typedef std::map<std::string, ErrorSpecialPolicy> ErrorSpecialPolicies;

struct ClimbTriggerPolicy {
  ErrorRatePolicy error_rate_;
  SlowRatePolicy slow_rate_;
  ErrorSpecialPolicies error_specials_;  // 特殊错误码触发调整配置

  void InitPolicy(const v1::ClimbConfig::TriggerPolicy& policy);
};

struct ClimbThrottling {
  ClimbThrottling()
      : cold_below_tune_down_rate_(0), cold_below_tune_up_rate_(0), cold_above_tune_down_rate_(0),
        cold_above_tune_up_rate_(0), limit_threshold_to_tune_up_(0), judge_duration_(0),
        tune_up_period_(0), tune_down_period_(0) {}
  int32_t cold_below_tune_down_rate_;  //冷水位以下区间的下调百分比
  int32_t cold_below_tune_up_rate_;    //冷水位以下区间的上调百分比
  int32_t cold_above_tune_down_rate_;  //冷水位以上区间的下调百分比
  int32_t cold_above_tune_up_rate_;    //冷水位以上区间的上调百分比
  int32_t limit_threshold_to_tune_up_;  //冷水位以上，超过百分之多少的请求被限流后，才进行阈值上调
  uint64_t judge_duration_;  //阈值调整规则的决策间隔
  int32_t tune_up_period_;  //阈值上调周期数，连续N个决策间隔都为上调，才执行上调
  int32_t tune_down_period_;  //阈值下调周期数，连续N个决策间隔都为下调，才执行下调

  void InitClimbThrottling(const v1::ClimbConfig::ClimbThrottling& climb_throttling);
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_CLIMB_CONFIG_H_
