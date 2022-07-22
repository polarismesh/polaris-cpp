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

#include "quota/adjuster/climb_config.h"

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/wrappers.pb.h>
#include <stdint.h>
#include <v1/ratelimit.pb.h>

#include "utils/time_clock.h"

namespace polaris {

#define GET_DURATION(data, flied, default_value)                                                                  \
  data.has_##flied() ? (data.flied().seconds() * Time::kThousandBase + data.flied().nanos() / Time::kMillionBase) \
                     : default_value

void ClimbMetricConfig::InitMetricConfig(const v1::ClimbConfig::MetricConfig& metric_config) {
  static const uint64_t kWindowSize = 60 * Time::kThousandBase;
  static const uint32_t kPrecision = 100;
  static const uint64_t kReportInterval = 20 * Time::kThousandBase;

  window_size_ = GET_DURATION(metric_config, window, kWindowSize);
  precision_ = metric_config.has_precision() ? metric_config.precision().value() : kPrecision;
  report_interval_ = GET_DURATION(metric_config, reportinterval, kReportInterval);
}

#define POLICY_FLIED(data, pb_flied, default_value) \
  policy.data().has_##pb_flied() ? policy.data().pb_flied().value() : default_value;

void ClimbTriggerPolicy::InitPolicy(const v1::ClimbConfig::TriggerPolicy& policy) {
  static const bool kEnable = true;
  static const uint32_t kRequestVolumeThreshold = 30;
  static const int32_t kErrorRate = 40;
  static const uint64_t kMaxRt = 5 * Time::kThousandBase;
  static const int32_t kSlowRate = 20;

  error_rate_.enable_ = POLICY_FLIED(errorrate, enable, kEnable);
  error_rate_.request_volume_threshold_ = POLICY_FLIED(errorrate, requestvolumethreshold, kRequestVolumeThreshold);
  error_rate_.error_rate_ = POLICY_FLIED(errorrate, errorrate, kErrorRate);

  slow_rate_.enable_ = POLICY_FLIED(slowrate, enable, kEnable);
  slow_rate_.max_rt_ = GET_DURATION(policy.slowrate(), maxrt, kMaxRt);
  slow_rate_.slow_rate_ = POLICY_FLIED(slowrate, slowrate, kSlowRate);

  for (int i = 0; i < policy.errorrate().specials_size(); ++i) {
    const v1::ClimbConfig::TriggerPolicy::ErrorRate::SpecialConfig& special = policy.errorrate().specials(i);
    ErrorSpecialPolicy& special_error = error_specials_[special.type().value()];
    for (int j = 0; j < special.errorcodes_size(); ++j) {
      special_error.error_codes_.insert(special.errorcodes(j).value());
    }
    special_error.error_rate_ = special.errorrate().value();
  }
}

#define THROTTLING_FLIED(flied, pb_flied, default_value) \
  flied = climb_throttling.has_##pb_flied() ? climb_throttling.pb_flied().value() : default_value;

void ClimbThrottling::InitClimbThrottling(const v1::ClimbConfig::ClimbThrottling& climb_throttling) {
  static const int32_t kColdBelowTuneDownRate = 75;
  static const int32_t kColdBelowTuneUpRate = 65;
  static const int32_t kColdAboveTuneDownRate = 95;
  static const int32_t kColdAboveTuneUpRate = 80;
  static const int32_t kLimitThresholdToTuneUp = 2;
  static const uint64_t kJudgeDuration = 10 * Time::kThousandBase;
  static const int32_t kTuneUpPeriod = 2;
  static const int32_t kTuneDownPeriod = 2;

  THROTTLING_FLIED(cold_below_tune_down_rate_, coldbelowtunedownrate, kColdBelowTuneDownRate)
  THROTTLING_FLIED(cold_below_tune_up_rate_, coldbelowtuneuprate, kColdBelowTuneUpRate)
  THROTTLING_FLIED(cold_above_tune_down_rate_, coldabovetunedownrate, kColdAboveTuneDownRate)
  THROTTLING_FLIED(cold_above_tune_up_rate_, coldabovetuneuprate, kColdAboveTuneUpRate)
  THROTTLING_FLIED(limit_threshold_to_tune_up_, limitthresholdtotuneup, kLimitThresholdToTuneUp)
  judge_duration_ = GET_DURATION(climb_throttling, judgeduration, kJudgeDuration);
  THROTTLING_FLIED(tune_up_period_, tuneupperiod, kTuneUpPeriod)
  THROTTLING_FLIED(tune_down_period_, tunedownperiod, kTuneDownPeriod)
}

}  // namespace polaris
