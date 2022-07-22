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

#ifndef POLARIS_CPP_POLARIS_CACHE_REPORT_CLIENT_H_
#define POLARIS_CPP_POLARIS_CACHE_REPORT_CLIENT_H_

#include "polaris/context.h"
#include "reactor/reactor.h"

namespace polaris {

/// @brief 客户端上报任务
///
/// 上报本机IP给服务发现Server获取Location信息
/// 启动时需要如果获取失败，需要立即重试，避免就近失效过久
class ReportClient {
 public:
  ReportClient(Context* context, Reactor& reactor)
      : context_(context), reactor_(&reactor), report_interval_(0), not_found_retry_times_(0), init_retry_times_(0) {}

  void SetupTask();

  void DoTask();

  static void DoTask(ReportClient* report_client) { report_client->DoTask(); }

  void Submit(uint64_t next_time) {
    reactor_->AddTimingTask(new TimingFuncTask<ReportClient>(DoTask, this, next_time));
  }

 private:
  Context* context_;
  Reactor* reactor_;

  // 任务上报间隔
  uint64_t report_interval_;

  // 启动时如果返回未找到位置信息时重试次数
  int not_found_retry_times_;

  // 启动时其他初始化失败重试次数
  int init_retry_times_;
};

class ReportTaskSubmit : public Task {
 public:
  explicit ReportTaskSubmit(ReportClient* report_client, uint64_t next_time)
      : report_client_(report_client), next_time_(next_time) {}

  virtual ~ReportTaskSubmit() {}

  virtual void Run() { report_client_->Submit(next_time_); }

 private:
  ReportClient* report_client_;
  uint64_t next_time_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CACHE_REPORT_CLIENT_H_
