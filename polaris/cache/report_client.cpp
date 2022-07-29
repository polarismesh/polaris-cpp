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

#include "cache/report_client.h"

#include "context/context_impl.h"
#include "logger.h"
#include "model/location.h"

namespace polaris {

void ReportClient::SetupTask() {
  ContextImpl* context_impl = context_->GetContextImpl();
  init_retry_times_ = context_impl->GetApiMaxRetryTimes();
  not_found_retry_times_ = init_retry_times_;
  reactor_->SubmitTask(new FuncTask<ReportClient>(DoTask, this));
}

void ReportClient::DoTask() {
  ContextImpl* context_impl = context_->GetContextImpl();
  ServerConnector* server_connector = context_impl->GetServerConnector();
  POLARIS_ASSERT(server_connector != nullptr);

  PolarisCallback polaris_callback = [=](ReturnCode ret_code, const std::string& message,
                                         std::unique_ptr<v1::Response> response) {
    if (ret_code == kReturnOk) {  // 更新Location
      if (response->has_client() && response->client().has_location()) {
        const v1::Location& client_location = response->client().location();
        Location location;
        location.region = client_location.region().value();
        location.zone = client_location.zone().value();
        location.campus = client_location.campus().value();
        context_impl->GetClientLocation().Update(location);
        POLARIS_LOG(LOG_DEBUG, "sdk client location, region = %s, zone = %s, campus = %s", location.region.c_str(),
                    location.zone.c_str(), location.campus.c_str());
        context_impl->GetCacheManager()->GetCachePersist().PersistLocation(location);
      } else {
        ret_code = kReturnResourceNotFound;
      }
    }
    if (ret_code == kReturnResourceNotFound) {
      POLARIS_LOG(LOG_ERROR, "report client failed, retcode = %d, msg: %s", ret_code, message.c_str());
      if (not_found_retry_times_-- > 0) {  // 位置信息未找到时5s重试
        reactor_->SubmitTask(new ReportTaskSubmit(this, 5000));
        return;
      }
    } else if (ret_code != kReturnOk) {
      POLARIS_LOG(LOG_ERROR, "report client failed, retcode = %d, msg: %s", ret_code, message.c_str());
      if (init_retry_times_-- > 0) {  // 启动时失败需要立刻重试
        reactor_->SubmitTask(new FuncTask<ReportClient>(DoTask, this));
        return;
      }
    }
    // 设置定时任务
    reactor_->SubmitTask(new ReportTaskSubmit(this, context_impl->GetReportClientInterval()));
  };

  const std::string& bind_ip = context_impl->GetApiBindIp();
  if (bind_ip.empty()) {
    //TODO: 当前通过连接自动获取IP并设置后，这里会获取不到，后续需要解决
    reactor_->AddTimingTask(new TimingFuncTask<ReportClient>(DoTask, this, context_impl->GetReportClientInterval()));
    return;
  }
  ReturnCode ret_code =
      server_connector->AsyncReportClient(bind_ip, context_impl->GetApiDefaultTimeout(), polaris_callback);
  if (ret_code != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "report client failed, retcode = %d", ret_code);
    // 设置定时任务
    reactor_->AddTimingTask(new TimingFuncTask<ReportClient>(DoTask, this, context_impl->GetReportClientInterval()));
  }
}

}  // namespace polaris
