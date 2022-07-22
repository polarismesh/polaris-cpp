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

#include "api_stat.h"

#include <stddef.h>

#include "api_stat_registry.h"
#include "context/context_impl.h"
#include "utils/time_clock.h"

namespace polaris {

ApiStat::ApiStat(ContextImpl* context_impl, ApiStatKey stat_key)
    : registry_(context_impl->GetApiStatRegistry()), api_time_(Time::GetCoarseSteadyTimeMs()), stat_key_(stat_key) {}

void ApiStat::Record(ReturnCode ret_code) {
  if (registry_ != nullptr) {
    registry_->Record(stat_key_, ret_code, Time::GetCoarseSteadyTimeMs() - api_time_);
    registry_ = nullptr;
  }
}

}  // namespace polaris
