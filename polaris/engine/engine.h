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

#ifndef POLARIS_CPP_POLARIS_ENGINE_ENGINE_H_
#define POLARIS_CPP_POLARIS_ENGINE_ENGINE_H_

#include "cache/cache_manager.h"
#include "engine/circuit_breaker_executor.h"
#include "engine/health_check_executor.h"
#include "engine/main_executor.h"
#include "monitor/monitor_reporter.h"
#include "polaris/defs.h"

namespace polaris {

class Context;

class Engine {
public:
  explicit Engine(Context* context);

  ~Engine();

  ReturnCode Start();

  ReturnCode StopAndWait();

  CacheManager* GetCacheManager() { return &cache_manager_; }

  MonitorReporter* GetMonitorReporter() { return &monitor_reporter_; }

  CircuitBreakerExecutor* GetCircuitBreakerExecutor() { return &circuit_breaker_executor_; }

private:
  Context* context_;
  MainExecutor main_executor_;
  CacheManager cache_manager_;
  MonitorReporter monitor_reporter_;
  CircuitBreakerExecutor circuit_breaker_executor_;
  HealthCheckExecutor health_check_executor_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_ENGINE_ENGINE_H_
