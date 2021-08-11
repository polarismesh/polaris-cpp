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

namespace polaris {
namespace constants {

static const char kPolarisNamespace[] = "Polaris";

static const uint64_t kPolarisRefreshIntervalDefault = 10 * 60 * 1000;

static const char kBackupFileInstanceSuffix[]       = "instance";
static const char kBackupFileRoutingSuffix[]        = "routing";
static const char kBackupFileRateLimitSuffix[]      = "rate_limiting";
static const char kBackupFileCircuitBreakerSuffix[] = "circuit_breaker";

static const char kRouterRequestSetNameKey[] = "internal-set-name";
static const char kRouterRequestCanaryKey[]  = "internal-canary-name";

static const char kContainerNameKey[] = "container_name";

}  // namespace constants
}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_CONSTANTS_H_
