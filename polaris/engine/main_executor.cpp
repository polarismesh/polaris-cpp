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

#include "engine/main_executor.h"

#include <stddef.h>
#include <string>

#include "cache/cache_manager.h"
#include "cache/cache_persist.h"
#include "context_internal.h"
#include "logger.h"
#include "model/location.h"
#include "polaris/context.h"
#include "polaris/defs.h"
#include "polaris/plugin.h"
#include "reactor/reactor.h"
#include "reactor/task.h"

namespace polaris {

MainExecutor::MainExecutor(Context* context) : Executor(context), init_retry_times_(0) {}

void MainExecutor::SetupWork() {
  ContextImpl* context_impl = context_->GetContextImpl();
  init_retry_times_         = context_impl->GetApiMaxRetryTimes();
  reactor_.SubmitTask(new FuncTask<MainExecutor>(TimingReportClient, this));
}

void MainExecutor::TimingReportClient(MainExecutor* main_executor) {
  ContextImpl* context_impl         = main_executor->context_->GetContextImpl();
  ServerConnector* server_connector = main_executor->context_->GetServerConnector();
  POLARIS_ASSERT(server_connector != NULL);

  std::string bind_ip = context_impl->GetApiBindIp();
  POLARIS_ASSERT(!bind_ip.empty());
  Location location;
  ReturnCode retcode =
      server_connector->ReportClient(bind_ip, context_impl->GetApiDefaultTimeout(), location);
  if (retcode == kReturnOk) {  // 更新Location
    context_impl->GetClientLocation().Update(location);
    POLARIS_LOG(LOG_TRACE, "sdk client location, region = %s, zone = %s, campus = %s",
                location.region.c_str(), location.zone.c_str(), location.campus.c_str());
    context_impl->GetCacheManager()->GetCachePersist().PersistLocation(location);
  } else {
    POLARIS_LOG(LOG_ERROR, "report client failed, retcode = %d", retcode);
    if (main_executor->init_retry_times_ > 0) {  // 启动时失败需要立刻重试
      main_executor->init_retry_times_--;
      main_executor->reactor_.SubmitTask(
          new FuncTask<MainExecutor>(TimingReportClient, main_executor));
      return;
    }
  }
  // 设置定时任务
  main_executor->reactor_.AddTimingTask(new TimingFuncTask<MainExecutor>(
      TimingReportClient, main_executor, context_impl->GetReportClientInterval()));
}

}  // namespace polaris
