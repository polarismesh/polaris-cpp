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

#include "plugin/server_connector/timeout_strategy.h"

namespace polaris {

void TimeoutStrategy::Init(uint64_t min_timeout, uint64_t max_timeout, float expand) {
  min_timeout_ = min_timeout;
  timeout_     = min_timeout;
  max_timeout_ = max_timeout;
  expand_      = expand;
}

void TimeoutStrategy::SetNextRetryTimeout() {
  timeout_ = static_cast<uint64_t>(timeout_ * expand_);
  if (timeout_ > max_timeout_) {
    timeout_ = max_timeout_;
  }
}

void TimeoutStrategy::SetNormalTimeout(uint64_t time_used) {
  timeout_ = static_cast<uint64_t>(time_used * expand_);
  if (timeout_ < min_timeout_) {
    timeout_ = min_timeout_;
  } else if (timeout_ > max_timeout_) {
    timeout_ = max_timeout_;
  }
}

}  // namespace polaris
