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

#include "engine/health_check_executor.h"

#include <iosfwd>
#include <vector>

#include "context_internal.h"
#include "polaris/context.h"
#include "reactor/reactor.h"
#include "reactor/task.h"

namespace polaris {

HealthCheckExecutor::HealthCheckExecutor(Context* context) : Executor(context) {}

void HealthCheckExecutor::SetupWork() {
  reactor_.SubmitTask(new FuncTask<HealthCheckExecutor>(TimingDetect, this));
}

void HealthCheckExecutor::TimingDetect(HealthCheckExecutor* executor) {
  std::vector<ServiceContext*> all_service_contexts;
  executor->context_->GetContextImpl()->GetAllServiceContext(all_service_contexts);
  for (std::size_t i = 0; i < all_service_contexts.size(); ++i) {
    HealthCheckerChain* health_checker_chain = all_service_contexts[i]->GetHealthCheckerChain();
    health_checker_chain->DetectInstance();
    all_service_contexts[i]->DecrementRef();
  }
  all_service_contexts.clear();
  // 设置定时任务
  executor->reactor_.AddTimingTask(
      new TimingFuncTask<HealthCheckExecutor>(TimingDetect, executor, 1000));
}

}  // namespace polaris
