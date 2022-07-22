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

#include "quota/adjuster/quota_adjuster.h"

#include <stddef.h>
#include "quota/adjuster/climb_adjuster.h"
#include "quota/rate_limit_window.h"

namespace v1 {
class RateLimitRecord;
}

namespace polaris {

class MetricConnector;
class Reactor;

QuotaAdjuster::QuotaAdjuster(Reactor& reactor, MetricConnector* connector, RemoteAwareBucket* remote_bucket)
    : reactor_(reactor), connector_(connector), remote_bucket_(remote_bucket) {}

QuotaAdjuster::~QuotaAdjuster() {
  connector_ = nullptr;
  remote_bucket_ = nullptr;
}

QuotaAdjuster* QuotaAdjuster::Create(QuotaAdjusterType quota_type, RateLimitWindow* window) {
  QuotaAdjuster* quota_adjuster = nullptr;
  if (quota_type == kQuotaAdjusterClimb) {
    quota_adjuster = new ClimbAdjuster(window->GetReactor(), window->GetMetricConnector(), window->GetRemoteBucket());
    if (quota_adjuster->Init(window->GetRateLimitRule()) != kReturnOk) {
      quota_adjuster->DecrementRef();
      quota_adjuster = nullptr;
    }
  }
  return quota_adjuster;
}

}  // namespace polaris
