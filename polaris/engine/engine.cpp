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

#include "engine/engine.h"

#include "logger.h"

namespace polaris {

Engine::Engine(Context* context)
    : context_(context),
      cache_manager_(context),
      monitor_reporter_(context_),
      circuit_breaker_executor_(context),
      health_checker_executor_(context) {}

Engine::~Engine() {
  StopAndWait();
  context_ = nullptr;
}

ReturnCode Engine::Start() {
  POLARIS_ASSERT(context_ != nullptr);
  ReturnCode ret_code;
  if ((ret_code = cache_manager_.Start()) != kReturnOk || (ret_code = monitor_reporter_.Start()) != kReturnOk ||
      (ret_code = circuit_breaker_executor_.Start()) != kReturnOk ||
      (ret_code = health_checker_executor_.Start()) != kReturnOk) {
    return ret_code;
  }
  return kReturnOk;
}

ReturnCode Engine::StopAndWait() {
  cache_manager_.StopAndWait();
  monitor_reporter_.StopAndWait();
  circuit_breaker_executor_.StopAndWait();
  health_checker_executor_.StopAndWait();
  return kReturnOk;
}

}  // namespace polaris
