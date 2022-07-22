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

#include "engine/circuit_breaker_executor.h"

#include <stddef.h>

#include <vector>

#include "context/context_impl.h"
#include "context/service_context.h"
#include "metric/metric_connector.h"
#include "polaris/context.h"
#include "reactor/reactor.h"
#include "reactor/task.h"

namespace polaris {

CircuitBreakerExecutor::CircuitBreakerExecutor(Context* context) : Executor(context) {
  metric_connector_ = new MetricConnector(reactor_, context);
}

void CircuitBreakerExecutor::SetMetricConnector(MetricConnector* connector) {
  if (metric_connector_ != nullptr) {
    delete metric_connector_;
  }
  metric_connector_ = connector;
}

CircuitBreakerExecutor::~CircuitBreakerExecutor() {
  if (metric_connector_ != nullptr) {
    delete metric_connector_;
  }
}

void CircuitBreakerExecutor::SetupWork() {
  reactor_.SubmitTask(new FuncTask<CircuitBreakerExecutor>(TimingCircuitBreak, this));
}

void CircuitBreakerExecutor::TimingCircuitBreak(CircuitBreakerExecutor* executor) {
  std::vector<std::shared_ptr<ServiceContext>> all_service_contexts;
  auto context_impl = executor->context_->GetContextImpl();
  context_impl->GetAllServiceContext(all_service_contexts);
  for (auto& service_context : all_service_contexts) {
    InstanceExistChecker exist_checker = [&](const std::string& instance_id) {
      context_impl->RcuEnter();
      bool result = service_context->CheckInstanceExist(instance_id);
      context_impl->RcuExit();
      return result;
    };
    service_context->GetCircuitBreakerChain()->TimingCircuitBreak(exist_checker);
  }
  all_service_contexts.clear();
  // 设置定时任务
  executor->reactor_.AddTimingTask(new TimingFuncTask<CircuitBreakerExecutor>(TimingCircuitBreak, executor, 1000));
}

}  // namespace polaris
