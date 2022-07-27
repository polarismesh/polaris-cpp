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

#include "quota/rate_limit_connector.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <v1/code.pb.h>
#include <v1/request.pb.h>
#include <v2/ratelimit_v2.pb.h>

#include <utility>

#include "api/consumer_api.h"
#include "context/context_impl.h"
#include "logger.h"
#include "polaris/context.h"
#include "polaris/log.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "rate_limit_window.h"
#include "reactor/reactor.h"
#include "utils/time_clock.h"

namespace polaris {

WindowSyncTask::WindowSyncTask(RateLimitWindow* window, RateLimitConnector* connector, uint64_t timeout)
    : TimingTask(timeout), window_(window), connector_(connector) {
  window_->IncrementRef();
}

WindowSyncTask::~WindowSyncTask() { window_->DecrementRef(); }

void WindowSyncTask::Run() { connector_->SyncTask(window_); }

WindowSyncTaskSet::WindowSyncTaskSet(RateLimitConnector* connector, uint64_t timeout)
    : TimingTask(timeout), connector_(connector) {}

void WindowSyncTaskSet::AddWindow(RateLimitWindow* window) {
  if (window_set_.find(window) == window_set_.end()) {
    window->IncrementRef();
    window_set_.insert(window);
  }
}

void WindowSyncTaskSet::Run() {
  for (std::set<RateLimitWindow*>::iterator it = window_set_.begin(); it != window_set_.end(); ++it) {
    connector_->SyncTask(*it);
  }
}

WindowSyncTaskSet::~WindowSyncTaskSet() {
  for (std::set<RateLimitWindow*>::iterator it = window_set_.begin(); it != window_set_.end(); ++it) {
    (*it)->DecrementRef();
  }
  window_set_.clear();
}

void WindowSyncTimeoutCheck::Run() { connection_->OnResponseTimeout(window_, task_type_); }

bool operator<(const LimitTargetKey& lhs, const LimitTargetKey& rhs) {
  if (lhs.labels_ < rhs.labels_) {
    return true;
  } else if (lhs.labels_ > rhs.labels_) {
    return false;
  } else {
    return lhs.service_key_ < rhs.service_key_;
  }
}

// ---------------------------------------------------------------------------

RateLimitConnection::RateLimitConnection(RateLimitConnector& connector, const uint64_t& request_timeout,
                                         Instance* instance, const ServiceKey& cluster, const std::string& id)
    : connector_(connector),
      reactor_(connector.GetReactor()),
      request_timeout_(request_timeout),
      cluster_(cluster),
      instance_(instance),
      connection_id_(id),
      stream_(nullptr),
      last_used_time_(Time::GetCoarseSteadyTimeMs()),
      last_response_time_(last_used_time_),
      is_closing_(false),
      sync_time_task_(reactor_.TimingTaskEnd()),
      time_diff_(0),
      sync_time_stream_(nullptr),
      client_key_(0),
      batch_task_(reactor_.TimingTaskEnd()) {
  client_ = new grpc::GrpcClient(reactor_);
  // 初始化完毕后发起异步请求
  client_->Connect(instance_->GetHost(), instance_->GetPort(), 1000,
                   std::bind(&RateLimitConnection::OnConnect, this, std::placeholders::_1));
}

RateLimitConnection::~RateLimitConnection() {
  stream_ = nullptr;
  if (instance_ != nullptr) {
    delete instance_;
    instance_ = nullptr;
  }
  if (client_ != nullptr) {
    delete client_;
    client_ = nullptr;
  }
  ClearTaskAndWindow();
}

void RateLimitConnection::ClearTaskAndWindow() {
  // 清除任务
  TimingTaskIter timeout_end = reactor_.TimingTaskEnd();
  if (sync_time_task_ != timeout_end) {
    reactor_.CancelTimingTask(sync_time_task_);
    sync_time_task_ = timeout_end;
  }
  for (std::map<RateLimitWindow*, TimingTaskIter>::iterator it = init_task_map_.begin(); it != init_task_map_.end();
       ++it) {
    if (it->second != timeout_end) {
      reactor_.CancelTimingTask(it->second);
    }
    it->first->DecrementRef();
  }
  init_task_map_.clear();
  for (std::map<RateLimitWindow*, WindowReportInfo>::iterator it = report_task_map_.begin();
       it != report_task_map_.end(); ++it) {
    if (it->second.task_iter_ != timeout_end) {
      reactor_.CancelTimingTask(it->second.task_iter_);
    }
    it->first->DecrementRef();
  }
  report_task_map_.clear();
  if (batch_task_ != timeout_end) {
    reactor_.CancelTimingTask(batch_task_);
  }
}

void RateLimitConnection::OnConnect(ReturnCode return_code) {
  if (is_closing_) {
    return;  // 说明已经被别的回调触发了连接关闭
  }
  if (return_code != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "rate limit connect to server[%s] return %d", client_->CurrentServer().c_str(), return_code);
    this->CloseForError(kServerCodeConnectError);
    return;
  }
  last_response_time_ = Time::GetCoarseSteadyTimeMs();
  // 连接成功，发送所有待发送的请求
  DoSyncTimeTask();
  stream_ = client_->StartStream("/polaris.metric.v2.RateLimitGRPCV2/Service", *this);

  SendPendingInit();  // 连接成功，进行批量初始化
}

void RateLimitConnection::DoSyncTimeTask() {
  sync_time_task_ = reactor_.TimingTaskEnd();
  if (is_closing_) {
    return;
  }
  metric::v2::TimeAdjustRequest request;
  sync_time_stream_ =
      client_->SendRequest(request, "/polaris.metric.v2.RateLimitGRPCV2/TimeAdjust", request_timeout_, *this);
  sync_time_task_ = reactor_.AddTimingTask(new TimeSyncTask(this, kTimeSyncTaskTimeoutCheck, request_timeout_));
}

void RateLimitConnection::OnSyncTimeTimeout() {
  sync_time_task_ = reactor_.TimingTaskEnd();
  if (is_closing_) {
    return;
  }
  // 同步任务超时
  POLARIS_LOG(LOG_ERROR, "rate limit sync time to [%s] failed", client_->CurrentServer().c_str());
  this->CloseForError(kServerCodeRpcTimeout);
}

void RateLimitConnection::OnSuccess(metric::v2::TimeAdjustResponse* response) {
  if (is_closing_ || sync_time_task_ == reactor_.TimingTaskEnd()) {
    delete response;
    return;
  }
  uint64_t delay = CalculateRequestDelay(sync_time_task_);
  reactor_.CancelTimingTask(sync_time_task_);  // 取消超时检查
  if (response->servertimestamp() > 0) {       // 调整时间差
    int64_t server_time = response->servertimestamp() + delay / 2;
    int64_t current_time = static_cast<int64_t>(Time::GetSystemTimeMs());
    time_diff_ = server_time - current_time;
  }
  POLARIS_LOG(LOG_TRACE, "sync time diff:%" PRId64 "", time_diff_);
  delete response;
  if (sync_time_stream_ != nullptr) {  // 释放长链接上的request stream
    client_->DeleteStream(sync_time_stream_);
    sync_time_stream_ = nullptr;
  }
  connector_.UpdateCallResult(cluster_, instance_, delay, kServerCodeReturnOk);
  // 设置下一次同步任务
  sync_time_task_ = reactor_.AddTimingTask(new TimeSyncTask(this, kTimeSyncTaskTiming, 60 * 1000));
}

void RateLimitConnection::OnFailure(const std::string& message) {
  if (is_closing_) {
    return;  // 说明已经被别的回调触发了连接关闭
  }
  POLARIS_STAT_LOG(LOG_WARN, "send time sync to metric server %s failed with rpc error %s",
                   client_->CurrentServer().c_str(), message.c_str());
  this->CloseForError(kServerCodeRpcError);
}

void RateLimitConnection::OnReceiveMessage(metric::v2::RateLimitResponse* response) {
  if (is_closing_) {
    delete response;
    return;  // 说明已经被别的回调触发了连接关闭
  }
  last_response_time_ = Time::GetCoarseSteadyTimeMs();
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    POLARIS_LOG(LOG_TRACE, "rate limit response %s", response->ShortDebugString().c_str());
  }
  if (response->cmd() == metric::v2::INIT) {
    const metric::v2::RateLimitInitResponse& init_response = response->ratelimitinitresponse();
    if (init_response.code() == v1::ExecuteSuccess) {
      this->OnInitResponse(init_response);
    } else {
      POLARIS_LOG(LOG_WARN, "rate limit init response error: %s", init_response.ShortDebugString().c_str());
    }
  } else if (response->cmd() == metric::v2::ACQUIRE) {
    const metric::v2::RateLimitReportResponse& report_response = response->ratelimitreportresponse();
    if (report_response.code() == v1::ExecuteSuccess) {
      this->OnReportResponse(report_response);
    } else {
      POLARIS_LOG(LOG_WARN, "rate limit report response error: %s", report_response.ShortDebugString().c_str());
    }
  } else if (response->cmd() == metric::v2::BATCH_INIT) {
    const metric::v2::RateLimitBatchInitResponse& batch_init_response = response->ratelimitbatchinitresponse();
    if (batch_init_response.code() == v1::ExecuteSuccess) {
      this->OnBatchInitResponse(batch_init_response);
    } else {
      POLARIS_LOG(LOG_WARN, "rate limit batch init response error: %s", batch_init_response.ShortDebugString().c_str());
    }
  } else if (response->cmd() == metric::v2::BATCH_ACQUIRE) {
    const metric::v2::RateLimitReportResponse& batch_report_response = response->ratelimitreportresponse();
    if (batch_report_response.code() == v1::ExecuteSuccess) {
      this->OnBatchReportResponse(batch_report_response);
    } else {
      POLARIS_LOG(LOG_WARN, "rate limit batch report response error: %s",
                  batch_report_response.ShortDebugString().c_str());
    }
  } else {
    POLARIS_LOG(LOG_WARN, "rate limit response with cmd [%d] not found", response->cmd());
  }
  delete response;
}

void RateLimitConnection::OnRemoteClose(const std::string& message) {
  if (is_closing_) {
    return;  // 说明已经被别的回调触发了连接关闭
  }
  POLARIS_LOG(LOG_ERROR, "rate limit stream to server[%s] closed with %s", client_->CurrentServer().c_str(),
              message.c_str());
  this->CloseForError(kServerCodeRemoteClose);
}

void RateLimitConnection::DoSyncTask(RateLimitWindow* window) {
  last_used_time_ = Time::GetCoarseSteadyTimeMs();
  if (report_task_map_.count(window) > 0) {
    SendReport(window);
  } else if (init_task_map_.count(window) > 0) {
    SendInit(window);
  } else {
    window->IncrementRef();
    if (stream_ != nullptr) {
      SendInit(window);
    } else {
      init_task_map_[window] = reactor_.TimingTaskEnd();
    }
  }
}

void RateLimitConnection::SetReportTask(RateLimitWindow* window, uint64_t next_report_interval, bool batch_report) {
  last_used_time_ = Time::GetCoarseSteadyTimeMs();
  POLARIS_ASSERT(init_task_map_.count(window) == 0);  // 已经Init过
  POLARIS_ASSERT(report_task_map_.count(window) == 1);
  if (window->EnableBatch() && batch_report) {
    if (connector_.IsConnectionChange(window)) {
      // 服务器变化，重新提交同步请求选择新服务器
      reactor_.AddTimingTask(new WindowSyncTask(window, &connector_));
    } else {
      batch_report_pending_.push_back(window);
    }
    return;
  }
  reactor_.AddTimingTask(new WindowSyncTask(window, &connector_, next_report_interval));
}

void RateLimitConnection::SendPendingInit() {
  // 组装批量初始化请求，同一个规则的窗口汇总在一个初始化子请求中
  int send_init_count = 0;
  int batch_count = 0;
  metric::v2::RateLimitRequest request;
  request.set_cmd(metric::v2::BATCH_INIT);
  metric::v2::RateLimitBatchInitRequest* batch_init = request.mutable_ratelimitbatchinitrequest();
  batch_init->set_clientid(connector_.GetContextId());
  LimitTargetKey target_key;
  std::map<RateLimitRule*, metric::v2::RateLimitInitRequest*> request_map;
  std::map<RateLimitRule*, metric::v2::RateLimitInitRequest*>::iterator request_it;
  for (std::map<RateLimitWindow*, TimingTaskIter>::iterator it = init_task_map_.begin(); it != init_task_map_.end();
       ++it) {
    if (!it->first->EnableBatch()) {
      SendInit(it->first);  // 窗口未开启批量上报，单独初始化
      send_init_count++;
      continue;
    }

    // 窗口开启批量上报，将窗口添加到批量初始化请求
    batch_count++;
    metric::v2::RateLimitInitRequest* init_request = nullptr;
    RateLimitRule* rule = it->first->GetRateLimitRule();
    if ((request_it = request_map.find(rule)) != request_map.end()) {
      init_request = request_it->second;
    } else {
      init_request = batch_init->add_request();
      request_map[rule] = init_request;
      it->first->GetInitRequest(init_request);
      init_request->mutable_target()->clear_labels();
    }
    target_key.service_key_.namespace_ = init_request->target().namespace_();
    target_key.service_key_.name_ = init_request->target().service();
    const std::string& metric_id = it->first->GetMetricId();
    std::size_t sharp_index = metric_id.find_first_of("#");
    target_key.labels_ = metric_id.substr(sharp_index + 1);
    limit_target_map_[target_key] = it->first;

    init_request->mutable_target()->add_labels_list(target_key.labels_);
  }
  if (send_init_count > 0) {
    POLARIS_LOG(LOG_INFO, "rate limit connect to server[%s] success, send %d pending init request",
                client_->CurrentServer().c_str(), send_init_count);
  }
  if (batch_count > 0) {  // 发送批量初始化请求，并设置请求超时检查任务
    stream_->SendMessage(request, false);
    batch_task_ =
        reactor_.AddTimingTask(new WindowSyncTimeoutCheck(nullptr, this, kWindowBatchInitTask, request_timeout_));
    POLARIS_LOG(LOG_INFO, "rate limit connect to [%s] success, send %d window batch init size %zu",
                client_->CurrentServer().c_str(), batch_count, request.ByteSizeLong());
  } else {  // 当前没有批量初始化窗口，直接设置批量上报定时任务
    batch_task_ = reactor_.AddTimingTask(new TimingFuncTask<RateLimitConnection>(RateLimitConnection::SendBatchReport,
                                                                                 this, connector_.GetBatchInterval()));
    POLARIS_LOG(LOG_INFO, "rate limit connect to [%s] success, setup timing batch report task",
                client_->CurrentServer().c_str());
  }
}

void RateLimitConnection::SendBatchReport() {
  metric::v2::RateLimitRequest request;
  request.set_cmd(metric::v2::BATCH_ACQUIRE);
  metric::v2::RateLimitReportRequest* report_request = request.mutable_ratelimitreportrequest();
  report_request->set_clientkey(client_key_);

  POLARIS_ASSERT(batch_report_inflight_.empty());
  batch_report_inflight_.swap(batch_report_pending_);
  for (std::size_t i = 0; i < batch_report_inflight_.size(); ++i) {
    batch_report_inflight_[i]->GetReportRequest(report_request);
  }

  POLARIS_LOG(LOG_TRACE, "window batch size %zu", batch_report_inflight_.size());

  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    POLARIS_LOG(LOG_TRACE, "window batch report with request: %s", request.ShortDebugString().c_str());
  }
  stream_->SendMessage(request, false);
  // 设置超时检查
  batch_task_ =
      reactor_.AddTimingTask(new WindowSyncTimeoutCheck(nullptr, this, kWindowBatchReportTask, request_timeout_));
}

void RateLimitConnection::SendBatchReport(RateLimitConnection* connection) {
  if (connection->batch_report_pending_.empty()) {
    connection->batch_task_ = connection->reactor_.AddTimingTask(new TimingFuncTask<RateLimitConnection>(
        RateLimitConnection::SendBatchReport, connection, connection->connector_.GetBatchInterval()));
    return;
  }
  connection->SendBatchReport();
}

void RateLimitConnection::RemoveWindow(RateLimitWindow* window) {
  last_used_time_ = Time::GetCoarseSteadyTimeMs();
  std::map<RateLimitWindow*, WindowReportInfo>::iterator it = report_task_map_.find(window);
  if (it != report_task_map_.end()) {
    for (std::size_t i = 0; i < it->second.counter_keys_.size(); ++i) {
      // 需要检查下映射的窗口还是不是自己，避免删除相同规则因修改配额而触发重新建立的映射
      auto counter_key_map_it = counter_key_map_.find(it->second.counter_keys_[i]);
      if (counter_key_map_it != counter_key_map_.end() && counter_key_map_it->second == window) {
        counter_key_map_.erase(counter_key_map_it);
      }
    }
    if (it->second.task_iter_ != reactor_.TimingTaskEnd()) {  // 取消超时检查任务
      reactor_.CancelTimingTask(it->second.task_iter_);
    }
    report_task_map_.erase(it);
    window->DecrementRef();
  } else {
    std::map<RateLimitWindow*, TimingTaskIter>::iterator init_it = init_task_map_.find(window);
    if (init_it != init_task_map_.end()) {
      if (init_it->second != reactor_.TimingTaskEnd()) {  // 取消超时检查任务
        reactor_.CancelTimingTask(init_it->second);
      }
      init_task_map_.erase(init_it);
      window->DecrementRef();
    }
  }
}

void RateLimitConnection::SendInit(RateLimitWindow* window) {
  metric::v2::RateLimitRequest request;
  request.set_cmd(metric::v2::INIT);
  metric::v2::RateLimitInitRequest* init_request = request.mutable_ratelimitinitrequest();
  window->GetInitRequest(init_request);
  init_request->set_clientid(connector_.GetContextId());
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    POLARIS_LOG(LOG_TRACE, "window init with request: %s", request.ShortDebugString().c_str());
  }
  stream_->SendMessage(request, false);
  // 设置超时检查
  LimitTargetKey target_key;
  const metric::v2::LimitTarget& limit_target = request.ratelimitinitrequest().target();
  target_key.service_key_.namespace_ = limit_target.namespace_();
  target_key.service_key_.name_ = limit_target.service();
  target_key.labels_ = limit_target.labels();
  limit_target_map_[target_key] = window;
  init_task_map_[window] =
      reactor_.AddTimingTask(new WindowSyncTimeoutCheck(window, this, kWindowSyncInitTask, request_timeout_));
}

void RateLimitConnection::SendReport(RateLimitWindow* window) {
  metric::v2::RateLimitRequest request;
  request.set_cmd(metric::v2::ACQUIRE);
  metric::v2::RateLimitReportRequest* report_request = request.mutable_ratelimitreportrequest();
  window->GetReportRequest(report_request);
  report_request->set_clientkey(client_key_);
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    POLARIS_LOG(LOG_TRACE, "window report with request: %s", request.ShortDebugString().c_str());
  }
  stream_->SendMessage(request, false);
  // 设置超时检查
  report_task_map_[window].task_iter_ =
      reactor_.AddTimingTask(new WindowSyncTimeoutCheck(window, this, kWindowSyncReportTask, request_timeout_));
}

void RateLimitConnection::OnInitResponse(const metric::v2::RateLimitInitResponse& response) {
  // 查找window
  LimitTargetKey target_key;
  const metric::v2::LimitTarget& limit_target = response.target();
  target_key.service_key_.namespace_ = limit_target.namespace_();
  target_key.service_key_.name_ = limit_target.service();
  target_key.labels_ = limit_target.labels();
  std::map<LimitTargetKey, RateLimitWindow*>::iterator target_it = limit_target_map_.find(target_key);
  if (target_it != limit_target_map_.end()) {
    RateLimitWindow* window = target_it->second;
    std::map<RateLimitWindow*, TimingTaskIter>::iterator task_it = init_task_map_.find(window);
    if (task_it != init_task_map_.end() && task_it->second != reactor_.TimingTaskEnd()) {
      uint64_t delay = CalculateRequestDelay(task_it->second);
      reactor_.CancelTimingTask(task_it->second);  // 取消超时检查
      init_task_map_.erase(task_it);

      client_key_ = response.clientkey();
      WindowReportInfo& report_info = report_task_map_[window];
      for (int i = 0; i < response.counters_size(); ++i) {  // 处理索引
        counter_key_map_[response.counters(i).counterkey()] = window;
        report_info.counter_keys_.push_back(response.counters(i).counterkey());
      }
      report_info.task_iter_ = reactor_.TimingTaskEnd();  // 从init迁移到同步任务
      limit_target_map_.erase(target_key);
      window->OnInitResponse(response.counters(), response.timestamp(), time_diff_);

      connector_.UpdateCallResult(cluster_, instance_, delay, kServerCodeReturnOk);
      SetReportTask(window, window->GetRateLimitRule()->GetRateLimitReport().GetInterval());
      return;
    }
    limit_target_map_.erase(target_key);  // 窗口已经不再init流程中，删除对应的数据
  }
  // 请求已经超时了
  POLARIS_LOG(LOG_WARN, "init response for service [%s/%s] labels[%s] with timeout",
              target_key.service_key_.namespace_.c_str(), target_key.service_key_.name_.c_str(),
              target_key.labels_.c_str());
}

void RateLimitConnection::OnReportResponse(const metric::v2::RateLimitReportResponse& response) {
  if (response.quotalefts().empty()) {
    POLARIS_LOG(LOG_TRACE, "report with empty quota left response: %s", response.ShortDebugString().c_str());
    return;
  }
  int counter_key = response.quotalefts().begin()->counterkey();
  auto counter_it = counter_key_map_.find(counter_key);
  if (counter_it == counter_key_map_.end()) {
    POLARIS_LOG(LOG_TRACE, "report with counter key[%d] not exists, response: %s", counter_key,
                response.ShortDebugString().c_str());
    return;
  }
  RateLimitWindow* window = counter_it->second;
  auto task_it = report_task_map_.find(window);
  if (task_it != report_task_map_.end()) {
    if (task_it->second.task_iter_ != reactor_.TimingTaskEnd()) {
      uint64_t delay = CalculateRequestDelay(task_it->second.task_iter_);
      reactor_.CancelTimingTask(task_it->second.task_iter_);  // 取消超时检查
      connector_.UpdateCallResult(cluster_, instance_, delay, kServerCodeReturnOk);
      bool speed_up;
      uint64_t report_time = window->OnReportResponse(response, time_diff_, speed_up);
      bool batch_report = !speed_up && report_time <= connector_.GetBatchInterval();
      SetReportTask(window, report_time > delay ? report_time - delay : 0, batch_report);
    } else {  // 服务器主动推送的消息
      if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
        POLARIS_LOG(LOG_TRACE, "push response: %s", response.ShortDebugString().c_str());
      }
      bool speed_up;
      window->OnReportResponse(response, time_diff_, speed_up);
    }
    return;
  }
  // 请求已经超时了
  POLARIS_LOG(LOG_WARN, "window for counter key[%d] not exits", counter_key);
}

void RateLimitConnection::OnBatchInitResponse(const metric::v2::RateLimitBatchInitResponse& response) {
  POLARIS_ASSERT(batch_task_ != reactor_.TimingTaskEnd());
  uint64_t delay = CalculateRequestDelay(batch_task_);
  connector_.UpdateCallResult(cluster_, instance_, delay, kServerCodeReturnOk);
  reactor_.CancelTimingTask(batch_task_);

  client_key_ = response.clientkey();

  // 处理窗口初始化应答接口
  int succ_window = 0;
  int failed_window = 0;
  LimitTargetKey target_key;
  std::map<LimitTargetKey, RateLimitWindow*>::iterator target_it;
  std::map<RateLimitWindow*, TimingTaskIter>::iterator task_it;
  for (int result_index = 0; result_index < response.result_size(); ++result_index) {
    const metric::v2::BatchInitResult& init_result = response.result(result_index);
    const metric::v2::LimitTarget& limit_target = init_result.target();
    target_key.service_key_.namespace_ = limit_target.namespace_();
    target_key.service_key_.name_ = limit_target.service();

    if (init_result.code() != v1::ExecuteSuccess) {
      POLARIS_LOG(LOG_WARN, "batch init for target %s with error %d", init_result.target().ShortDebugString().c_str(),
                  init_result.code());
      // 处理失败的初始化任务
      for (int label_index = 0; label_index < limit_target.labels_list_size(); label_index++) {
        target_key.labels_ = limit_target.labels_list(label_index);
        if ((target_it = limit_target_map_.find(target_key)) != limit_target_map_.end()) {
          RateLimitWindow* window = target_it->second;
          if ((task_it = init_task_map_.find(window)) != init_task_map_.end()) {
            POLARIS_ASSERT(task_it->second == reactor_.TimingTaskEnd());
            reactor_.SubmitTask(new WindowSyncTask(window, &connector_));  // 重试初始化
          }
          limit_target_map_.erase(target_key);  // 删除等待应答的反向索引
          failed_window++;
        }
      }
      continue;
    }

    for (int counter_index = 0; counter_index < init_result.counters_size(); ++counter_index) {
      const metric::v2::LabeledQuotaCounter& labeled_counter = init_result.counters(counter_index);
      target_key.labels_ = labeled_counter.labels();
      if ((target_it = limit_target_map_.find(target_key)) != limit_target_map_.end()) {
        RateLimitWindow* window = target_it->second;
        if ((task_it = init_task_map_.find(window)) != init_task_map_.end()) {
          POLARIS_ASSERT(task_it->second == reactor_.TimingTaskEnd());
          init_task_map_.erase(task_it);

          WindowReportInfo& report_info = report_task_map_[window];
          for (int i = 0; i < labeled_counter.counters_size(); ++i) {  // 处理索引
            counter_key_map_[labeled_counter.counters(i).counterkey()] = window;
            report_info.counter_keys_.push_back(labeled_counter.counters(i).counterkey());
          }
          report_task_map_[window].task_iter_ = reactor_.TimingTaskEnd();  // 从init迁移到同步任务
          window->OnInitResponse(labeled_counter.counters(), response.timestamp(), time_diff_);
          SetReportTask(window, request_timeout_);
          succ_window++;
        }
        limit_target_map_.erase(target_key);  // 删除等待应答的反向索引
      }
    }
  }
  POLARIS_LOG(LOG_INFO, "rate limit batch init to server %s with success:%d failed:%d",
              client_->CurrentServer().c_str(), succ_window, failed_window);

  batch_task_ = reactor_.AddTimingTask(new TimingFuncTask<RateLimitConnection>(RateLimitConnection::SendBatchReport,
                                                                               this, connector_.GetBatchInterval()));
}

void RateLimitConnection::OnBatchReportResponse(const metric::v2::RateLimitReportResponse& response) {
  if (response.quotalefts().empty()) {
    POLARIS_LOG(LOG_ERROR, "batch report with empty quota left response: %s", response.ShortDebugString().c_str());
    return;
  }

  std::map<uint32_t, RateLimitWindow*>::iterator counter_it;
  std::map<RateLimitWindow*, std::vector<QuotaLeft> > quota_lefts;
  for (int i = 0; i < response.quotalefts_size(); ++i) {
    const metric::v2::QuotaLeft& left = response.quotalefts(i);
    if ((counter_it = counter_key_map_.find(left.counterkey())) != counter_key_map_.end()) {
      QuotaLeft quota_left;
      quota_left.counter_key_ = left.counterkey();
      quota_left.left_ = left.left();
      quota_lefts[counter_it->second].push_back(quota_left);
    }
  }

  POLARIS_ASSERT(batch_task_ != reactor_.TimingTaskEnd());
  uint64_t delay = CalculateRequestDelay(batch_task_);
  for (auto it = quota_lefts.begin(); it != quota_lefts.end(); ++it) {
    RateLimitWindow* window = it->first;
    auto task_it = report_task_map_.find(window);
    if (task_it == report_task_map_.end()) {
      continue;
    }
    bool speed_up;
    uint64_t report_time = window->OnReportResponse(it->second, response.timestamp(), time_diff_, speed_up);
    bool batch_report = !speed_up && report_time <= connector_.GetBatchInterval();
    SetReportTask(window, report_time > delay ? report_time - delay : 0, batch_report);
  }

  batch_report_inflight_.clear();
  reactor_.CancelTimingTask(batch_task_);  // 取消超时检查
  connector_.UpdateCallResult(cluster_, instance_, delay, kServerCodeReturnOk);

  batch_task_ = reactor_.AddTimingTask(new TimingFuncTask<RateLimitConnection>(
      RateLimitConnection::SendBatchReport, this,
      connector_.GetBatchInterval() > delay ? connector_.GetBatchInterval() - delay : 0));
}

void RateLimitConnection::OnResponseTimeout(RateLimitWindow* window, WindowSyncTaskType task_type) {
  if (task_type == kWindowBatchInitTask) {
    POLARIS_LOG(LOG_WARN, "batch init response with timeout");
    POLARIS_ASSERT(window == nullptr);
    // 这里检查初始化任务一定存在，但上报任务可能存在，因为存在有部分上报任务后突然来了一批初始化请求
    POLARIS_ASSERT(!init_task_map_.empty());
    batch_task_ = reactor_.TimingTaskEnd();
  } else if (task_type == kWindowBatchReportTask) {
    POLARIS_LOG(LOG_WARN, "batch report response with timeout");
    POLARIS_ASSERT(window == nullptr);
    batch_task_ = reactor_.TimingTaskEnd();
  } else if (task_type == kWindowSyncInitTask) {
    POLARIS_LOG(LOG_WARN, "init response for window %s with timeout", window->GetMetricId().c_str());
    init_task_map_[window] = reactor_.TimingTaskEnd();
  } else {
    POLARIS_LOG(LOG_WARN, "report response for window %s with timeout", window->GetMetricId().c_str());
    report_task_map_.erase(window);  // 移动到init task重新触发init
    init_task_map_[window] = reactor_.TimingTaskEnd();
  }
  connector_.UpdateCallResult(cluster_, instance_, request_timeout_, kServerCodeRpcTimeout);
  // 连接上的请求全部超时，切换连接
  this->CloseForError(kServerCodeRpcTimeout);
}

uint64_t RateLimitConnection::CalculateRequestDelay(const TimingTaskIter& iter) {
  // iter为超时检查任务，iter->first为请求超时对应时间
  return Time::GetCoarseSteadyTimeMs() + request_timeout_ - iter->first;
}

void RateLimitConnection::CloseForError(PolarisServerCode server_code) {
  if (is_closing_) {
    return;  // 说明已经被别的回调触发了连接关闭
  }
  is_closing_ = true;
  uint64_t delay = Time::GetCoarseSteadyTimeMs() - last_response_time_;
  connector_.UpdateCallResult(cluster_, instance_, delay, server_code);

  WindowSyncTaskSet* sync_task_set = new WindowSyncTaskSet(&connector_, 100);
  TimingTaskIter timeout_end = reactor_.TimingTaskEnd();
  std::map<RateLimitWindow*, TimingTaskIter>::iterator task_it;
  if (stream_ == nullptr) {  // 连接失败的情况
    for (std::map<RateLimitWindow*, TimingTaskIter>::iterator it = init_task_map_.begin(); it != init_task_map_.end();
         ++it) {
      sync_task_set->AddWindow(it->first);
    }
  } else {  // 连接成功后，请求超时
    for (std::map<RateLimitWindow*, TimingTaskIter>::iterator it = init_task_map_.begin(); it != init_task_map_.end();
         ++it) {
      if (it->second != timeout_end) {
        // 说明发送了初始化请求，正在等待应答,重设同步任务
        reactor_.CancelTimingTask(it->second);
        sync_task_set->AddWindow(it->first);
      } else if (it->first->EnableBatch()) {  // 没有请求在执行时，只有批量上报才重设任务
        sync_task_set->AddWindow(it->first);
      }
    }
    for (std::map<RateLimitWindow*, WindowReportInfo>::iterator it = report_task_map_.begin();
         it != report_task_map_.end(); ++it) {
      if (it->second.task_iter_ != timeout_end) {
        // 说明发送了单次上报请求，正在等待应答，重设同步任务
        sync_task_set->AddWindow(it->first);
        reactor_.CancelTimingTask(it->second.task_iter_);
      } else if (it->first->EnableBatch()) {  // 没有请求在执行时，只有批量上报才重设任务
        sync_task_set->AddWindow(it->first);
      }
    }
    if (batch_task_ != reactor_.TimingTaskEnd()) {
      reactor_.CancelTimingTask(batch_task_);
    }
  }
  reactor_.AddTimingTask(sync_task_set);

  connector_.EraseConnection(this->GetId());
  // 用于异步释放连接，不能在grpc相关回调中直接释放grpc client
  this->client_->Close();
  ClearTaskAndWindow();
  reactor_.SubmitTask(new DeferDeleteTask<RateLimitConnection>(this));
}

///////////////////////////////////////////////////////////////////////////////

RateLimitConnector::RateLimitConnector(Reactor& reactor, Context* context, uint64_t message_timeout,
                                       uint64_t batch_interval)
    : reactor_(reactor),
      context_(context),
      idle_check_interval_(10 * 1000),
      remove_after_idle_time_(60 * 1000),
      message_timeout_(message_timeout),
      batch_interval_(batch_interval) {
  // 提交定期空闲检查的任务
  reactor_.AddTimingTask(new TimingFuncTask<RateLimitConnector>(ConnectionIdleCheck, this, idle_check_interval_));
}

RateLimitConnector::~RateLimitConnector() {
  context_ = nullptr;
  for (std::map<std::string, RateLimitConnection*>::iterator it = connection_mgr_.begin(); it != connection_mgr_.end();
       ++it) {
    delete it->second;
  }
  connection_mgr_.clear();
}

void RateLimitConnector::SyncTask(RateLimitWindow* window) {
  if (window->IsExpired() || window->IsDeleted()) {
    std::map<std::string, RateLimitConnection*>::iterator it;
    if ((it = connection_mgr_.find(window->GetConnectionId())) != connection_mgr_.end()) {
      it->second->RemoveWindow(window);  // 从连接里释放window
    }
    return;
  }

  RateLimitConnection* connection = nullptr;
  ReturnCode ret = SelectConnection(window->GetMetricCluster(), window->GetMetricId(), connection);
  if (ret != kReturnOk) {  // 获取连接失败，稍后重试
    reactor_.AddTimingTask(new WindowSyncTask(window, this, 100 + rand() % 100));
    return;
  }
  if (window->GetConnectionId() != connection->GetId()) {  // 比较是否切换了服务器
    std::map<std::string, RateLimitConnection*>::iterator it;
    if ((it = connection_mgr_.find(window->GetConnectionId())) != connection_mgr_.end()) {
      it->second->RemoveWindow(window);  // 将窗口从旧连接清除
    }
    window->UpdateConnection(connection->GetId());
  }
  connection->DoSyncTask(window);
}

bool RateLimitConnector::IsConnectionChange(RateLimitWindow* window) {
  if (window->IsExpired() || window->IsDeleted()) {
    return true;
  }

  RateLimitConnection* connection = nullptr;
  ReturnCode ret = SelectConnection(window->GetMetricCluster(), window->GetMetricId(), connection);
  if (ret != kReturnOk) {  // 获取连接失败
    return true;
  }
  return window->GetConnectionId() != connection->GetId();
}

void RateLimitConnector::ConnectionIdleCheck(RateLimitConnector* connector) {
  uint64_t idle_check_time = Time::CoarseSteadyTimeSub(connector->remove_after_idle_time_);
  for (std::map<std::string, RateLimitConnection*>::iterator it = connector->connection_mgr_.begin();
       it != connector->connection_mgr_.end();) {
    if (it->second->IsIdle(idle_check_time)) {
      POLARIS_LOG(LOG_INFO, "free idle rate limit connection: %s", it->second->GetId().c_str());
      delete it->second;  // 过期释放连接
      connector->connection_mgr_.erase(it++);
    } else {
      ++it;
    }
  }
  // 提交下次检查的定时任务
  connector->reactor_.AddTimingTask(
      new TimingFuncTask<RateLimitConnector>(ConnectionIdleCheck, connector, connector->idle_check_interval_));
}

ReturnCode RateLimitConnector::InitService(const ServiceKey& service_key) {
  rate_limit_service_ = service_key;
  ReturnCode ret_code = kReturnOk;
  // 只有在LimitContext模式下且配置了限流服务时才立刻进行服务发现
  if (context_->GetContextMode() == kLimitContext && !service_key.name_.empty()) {
    Instance* instance = nullptr;
    uint64_t timeout = context_->GetContextImpl()->GetApiDefaultTimeout();
    Criteria criteria;
    ReturnCode ret_code = ConsumerApiImpl::GetSystemServer(context_, rate_limit_service_, criteria, instance, timeout);
    if (ret_code == kReturnOk) {
      delete instance;
    } else {
      POLARIS_LOG(LOG_ERROR, "init rate limit service[%s/%s] with error:%s", rate_limit_service_.namespace_.c_str(),
                  rate_limit_service_.name_.c_str(), ReturnCodeToMsg(ret_code).c_str());
    }
  }
  return ret_code;
}

const std::string& RateLimitConnector::GetContextId() { return context_->GetContextImpl()->GetSdkToken().uid(); }

ReturnCode RateLimitConnector::SelectInstance(const ServiceKey& metric_cluster, const std::string& hash_key,
                                              Instance** instance) {
  if (metric_cluster.name_.empty()) {  // TODO 这里暂时不输出日志，避免频繁刷日志
    return kReturnServiceNotFound;
  }
  // 通过一致性hash获取一个实例
  Criteria criteria;
  criteria.hash_string_ = hash_key;
  return ConsumerApiImpl::GetSystemServer(context_, metric_cluster, criteria, *instance, 0);
}

ReturnCode RateLimitConnector::SelectConnection(const ServiceKey& metric_cluster, const std::string& metric_id,
                                                RateLimitConnection*& connection) {
  Instance* instance = nullptr;  // 通过一致性hash获取一个实例
  const ServiceKey& cluster = metric_cluster.name_.empty() ? rate_limit_service_ : metric_cluster;
  ReturnCode ret_code = SelectInstance(cluster, metric_id, &instance);
  if (ret_code != kReturnOk) {
    return ret_code;
  }
  POLARIS_LOG(LOG_DEBUG, "select service[%s/%s] instance[%s:%d] for metric:%s", cluster.namespace_.c_str(),
              cluster.name_.c_str(), instance->GetHost().c_str(), instance->GetPort(), metric_id.c_str());
  std::string id = instance->GetHost() + ":" + std::to_string(instance->GetPort());
  std::map<std::string, RateLimitConnection*>::iterator it = connection_mgr_.find(id);
  if (it != connection_mgr_.end()) {  // 连接已存在，直接返回
    connection = it->second;
    delete instance;
  } else {
    connection = new RateLimitConnection(*this, message_timeout_, instance, cluster, id);
    connection_mgr_[connection->GetId()] = connection;
  }
  return kReturnOk;
}

void RateLimitConnector::UpdateCallResult(const ServiceKey& cluster, Instance* instance, uint64_t delay,
                                          PolarisServerCode server_code) {
  CallRetStatus status = kCallRetOk;  // 该服务不需要熔断
  ConsumerApiImpl::UpdateServerResult(context_, cluster, *instance, server_code, status, delay);
}

}  // namespace polaris
