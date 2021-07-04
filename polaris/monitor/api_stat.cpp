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
#include "context_internal.h"
#include "polaris/context.h"
#include "utils/time_clock.h"

namespace polaris {

ApiStat::ApiStat(Context* context, ApiStatKey stat_key) {
  registry_ = context->GetContextImpl()->GetApiStatRegistry();
  stat_key_ = stat_key;
  api_time_ = Time::GetCurrentTimeMs();
}

ApiStat::~ApiStat() {
  if (registry_ != NULL) {
    Record(kReturnOk);
    registry_ = NULL;
  }
}

void ApiStat::Record(ReturnCode ret_code) {
  if (registry_ == NULL) {
    return;
  }
  uint64_t current_time = Time::GetCurrentTimeMs();
  uint64_t time_used    = current_time >= api_time_ ? current_time - api_time_ : 0;
  registry_->Record(stat_key_, ret_code, time_used);
  registry_ = NULL;
}

}  // namespace polaris
