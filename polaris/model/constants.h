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

#ifndef POLARIS_CPP_POLARIS_MODEL_CONSTANTS_H_
#define POLARIS_CPP_POLARIS_MODEL_CONSTANTS_H_

#include <stdint.h>

#include <map>
#include <string>

namespace polaris {
namespace constants {

static const char kPolarisNamespace[] = "Polaris";

static const uint64_t kPolarisRefreshIntervalDefault = 10 * 60 * 1000;

static const char kBackupFileInstanceSuffix[] = "instance";
static const char kBackupFileRoutingSuffix[] = "routing";
static const char kBackupFileRateLimitSuffix[] = "rate_limiting";
static const char kBackupFileCircuitBreakerSuffix[] = "circuit_breaker";

static const char kRouterRequestSetNameKey[] = "internal-set-name";
static const char kRouterRequestCanaryKey[] = "internal-canary-name";

static const char kContainerNameKey[] = "container_name";

static const char kContinuousErrorThresholdKey[] = "continuousErrorThreshold";
static const int kContinuousErrorThresholdDefault = 10;

static const char kHalfOpenSleepWindowKey[] = "sleepWindow";
static const uint64_t kHalfOpenSleepWindowDefault = 30 * 1000;

static const char kRequestCountAfterHalfOpenKey[] = "requestCountAfterHalfOpen";
static const int kRequestCountAfterHalfOpenDefault = 10;

static const char kSuccessCountAfterHalfOpenKey[] = "successCountAfterHalfOpen";
static const int kSuccessCountAfterHalfOpenDefault = 8;

static const char kRequestVolumeThresholdKey[] = "requestVolumeThreshold";
static const int kRequestVolumeThresholdDefault = 10;

static const char kErrorRateThresholdKey[] = "errorRateThreshold";
static const float kErrorRateThresholdDefault = 0.5;

static const char kMetricStatTimeWindowKey[] = "metricStatTimeWindow";
static const uint64_t kMetricStatTimeWindowDefault = 60 * 1000;

static const char kMetricNumBucketsKey[] = "metricNumBuckets";
static const int kMetricNumBucketsDefault = 12;

static const char kMetricExpiredTimeKey[] = "metricExpiredTime";
static const uint64_t kMetricExpiredTimeDefault = 60 * 60 * 1000;

static const char kApiTimeoutKey[] = "timeout";
static const uint64_t kApiTimeoutDefault = 1000;

static const char kApiMaxRetryTimesKey[] = "maxRetryTimes";
static const uint64_t kApiMaxRetryTimesDefault = 5;

static const char kApiRetryIntervalKey[] = "retryInterval";
static const uint64_t kApiRetryIntervalDefault = 100;

static const char kApiBindIfKey[] = "bindIf";
static const char kApiBindIpKey[] = "bindIP";
static const char kClientReportIntervalKey[] = "reportInterval";
static const uint64_t kClientReportIntervalDefault = 10 * 60 * 1000;  // 10min

static const char kApiCacheClearTimeKey[] = "cacheClearTime";
static const uint64_t kApiCacheClearTimeDefault = 60 * 1000;  // 1min

// location key
static const char kApiLocationKey[] = "location";
static const char kLocationRegion[] = "region";
static const char kLocationZone[] = "zone";
static const char kLocationCampus[] = "campus";
static const char kLocationNone[] = "none";

// 全局的空字符串
const std::string& EmptyString();

// 全局的空字符串map
const std::map<std::string, std::string>& EmptyStringMap();

}  // namespace constants
}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_CONSTANTS_H_
