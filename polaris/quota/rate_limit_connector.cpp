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
#include "context_internal.h"
#include "logger.h"
#include "polaris/context.h"
#include "polaris/log.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "rate_limit_window.h"
#include "reactor/reactor.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

namespace polaris {

WindowSyncTask::WindowSyncTask(RateLimitWindow* window, RateLimitConnector* connector,
                               uint64_t timeout)
    : TimingTask(timeout), window_(window), connector_(connector) {
  window_->IncrementRef();
}

WindowSyncTask::~WindowSyncTask() { window_->DecrementRef(); }

void WindowSyncTask::Run() { connector_->SyncTask(window_); }

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

RateLimitConnection::RateLimitConnection(RateLimitConnector& connector,
                                         const uint64_t& request_timeout, Instance& instance,
                                         const ServiceKey& cluster, const std::string& id)
    : connector_(connector), reactor_(connector.GetReactor()), request_timeout_(request_timeout) {
  instance_id_   = instance.GetId();
  cluster_       = cluster;
  connection_id_ = id;
  client_        = new grpc::GrpcClient(reactor_);
  // 发起异步请求
  client_->ConnectTo(instance.GetHost(), instance.GetPort(), 1000,
                     new grpc::ConnectCallbackRef<RateLimitConnection>(*this));
  stream_             = NULL;
  last_used_time_     = Time::GetCurrentTimeMs();
  last_response_time_ = last_used_time_;
  is_closing_         = false;
  client_key_         = 0;

  sync_time_task_ = reactor_.TimingTaskEnd();
  time_diff_      = 0;
}

RateLimitConnection::~RateLimitConnection() {
  stream_ = NULL;
  if (client_ != NULL) {
    delete client_;
    client_ = NULL;
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
  for (std::map<RateLimitWindow*, TimingTaskIter>::iterator it = init_task_map_.begin();
       it != init_task_map_.end(); ++it) {
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
}

void RateLimitConnection::OnConnectSuccess() {
  if (is_closing_) {
    return;  // 说明已经被别的回调触发了连接关闭
  }
  last_response_time_ = Time::GetCurrentTimeMs();
  // 连接成功，发送所有待发送的请求
  DoSyncTimeTask();
  stream_ = client_->StartStream("/polaris.metric.v2.RateLimitGRPCV2/Service", *this);
  for (std::map<RateLimitWindow*, TimingTaskIter>::iterator task_it = init_task_map_.begin();
       task_it != init_task_map_.end(); ++task_it) {
    SendInit(task_it->first);
  }
  POLARIS_LOG(LOG_INFO, "rate limit connect to server[%s] success, send %zu pending init request",
              client_->CurrentServer().c_str(), init_task_map_.size());
}

void RateLimitConnection::OnConnectFailed() {
  if (is_closing_) {
    return;  // 说明已经被别的回调触发了连接关闭
  }
  POLARIS_LOG(LOG_ERROR, "rate limit connect to server[%s] failed",
              client_->CurrentServer().c_str());
  this->CloseForError();
}

void RateLimitConnection::OnConnectTimeout() {
  if (is_closing_) {
    return;  // 说明已经被别的回调触发了连接关闭
  }
  POLARIS_LOG(LOG_ERROR, "rate limit connect to server[%s] timeout",
              client_->CurrentServer().c_str());
  this->CloseForError();
}

void RateLimitConnection::DoSyncTimeTask() {
  sync_time_task_ = reactor_.TimingTaskEnd();
  if (is_closing_) {
    return;
  }
  metric::v2::TimeAdjustRequest request;
  client_->SendRequest(request, "/polaris.metric.v2.RateLimitGRPCV2/TimeAdjust", request_timeout_,
                       *this);
  sync_time_task_ =
      reactor_.AddTimingTask(new TimeSyncTask(this, kTimeSyncTaskTimoutCheck, request_timeout_));
}

void RateLimitConnection::OnSyncTimeTimeout() {
  sync_time_task_ = reactor_.TimingTaskEnd();
  if (is_closing_) {
    return;
  }
  // 同步任务超时
  POLARIS_LOG(LOG_ERROR, "rate limit sync time to [%s] failed", client_->CurrentServer().c_str());
  this->CloseForError();
}

void RateLimitConnection::OnSuccess(metric::v2::TimeAdjustResponse* response) {
  if (is_closing_ || sync_time_task_ == reactor_.TimingTaskEnd()) {
    delete response;
    return;
  }
  uint64_t delay = Time::GetCurrentTimeMs() + request_timeout_;
  delay          = delay > sync_time_task_->first ? delay - sync_time_task_->first : 0;
  reactor_.CancelTimingTask(sync_time_task_);  // 取消超时检查
  if (response->servertimestamp() > 0) {       // 调整时间差
    int64_t server_time   = response->servertimestamp() + delay / 2;
    uint64_t current_time = Time::GetCurrentTimeMs();
    time_diff_            = server_time - current_time;
  }
  POLARIS_LOG(LOG_TRACE, "sync time diff:%" PRId64 "", time_diff_);
  delete response;
  connector_.UpdateCallResult(cluster_, instance_id_, delay, kServerCodeReturnOk);
  // 设置下一次同步任务
  sync_time_task_ = reactor_.AddTimingTask(new TimeSyncTask(this, kTimeSyncTaskTiming, 60 * 1000));
}

void RateLimitConnection::OnFailure(grpc::GrpcStatusCode status, const std::string& message) {
  if (is_closing_) {
    return;  // 说明已经被别的回调触发了连接关闭
  }
  POLARIS_STAT_LOG(LOG_WARN, "send time sync to metric server %s failed with rpc error %d-%s",
                   client_->CurrentServer().c_str(), status, message.c_str());
  this->CloseForError();
}

void RateLimitConnection::OnReceiveMessage(metric::v2::RateLimitResponse* response) {
  if (is_closing_) {
    delete response;
    return;  // 说明已经被别的回调触发了连接关闭
  }
  last_response_time_ = Time::GetCurrentTimeMs();
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    POLARIS_LOG(LOG_TRACE, "rate limit response %s", response->ShortDebugString().c_str());
  }
  if (response->cmd() == metric::v2::INIT) {
    const metric::v2::RateLimitInitResponse& init_response = response->ratelimitinitresponse();
    if (init_response.code() == v1::ExecuteSuccess) {
      this->OnInitResponse(init_response);
    } else {
      POLARIS_LOG(LOG_WARN, "rate limit init response error: %s",
                  init_response.ShortDebugString().c_str());
    }
  } else if (response->cmd() == metric::v2::ACQUIRE) {
    const metric::v2::RateLimitReportResponse& report_response =
        response->ratelimitreportresponse();
    if (report_response.code() == v1::ExecuteSuccess) {
      this->OnReportResponse(report_response);
    } else {
      POLARIS_LOG(LOG_WARN, "rate limit report response error: %s",
                  report_response.ShortDebugString().c_str());
    }
  } else {
    POLARIS_LOG(LOG_WARN, "rate limit response with cmd [%d] not found", response->cmd());
  }
  delete response;
}

void RateLimitConnection::OnRemoteClose(grpc::GrpcStatusCode status, const std::string& message) {
  if (is_closing_) {
    return;  // 说明已经被别的回调触发了连接关闭
  }
  POLARIS_LOG(LOG_ERROR, "rate limit stream to server[%s] closed with %d-%s",
              client_->CurrentServer().c_str(), status, message.c_str());
  this->CloseForError();
}

void RateLimitConnection::DoSyncTask(RateLimitWindow* window) {
  last_used_time_ = Time::GetCurrentTimeMs();
  if (report_task_map_.count(window) > 0) {
    SendReprot(window);
  } else if (init_task_map_.count(window) > 0) {
    SendInit(window);
  } else {
    window->IncrementRef();
    if (stream_ != NULL) {
      SendInit(window);
    } else {
      init_task_map_[window] = reactor_.TimingTaskEnd();
    }
  }
}

void RateLimitConnection::RemoveWindow(RateLimitWindow* window) {
  last_used_time_                                           = Time::GetCurrentTimeMs();
  std::map<RateLimitWindow*, WindowReportInfo>::iterator it = report_task_map_.find(window);
  if (it != report_task_map_.end()) {
    for (std::size_t i = 0; i < it->second.counter_keys_.size(); ++i) {
      counter_key_map_.erase(it->second.counter_keys_[i]);
    }
    report_task_map_.erase(it);
    window->DecrementRef();
  } else if (init_task_map_.count(window) > 0) {
    init_task_map_.erase(window);
    window->DecrementRef();
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
  target_key.service_key_.namespace_          = limit_target.namespace_();
  target_key.service_key_.name_               = limit_target.service();
  target_key.labels_                          = limit_target.labels();
  limit_target_map_[target_key]               = window;
  init_task_map_[window]                      = reactor_.AddTimingTask(
      new WindowSyncTimeoutCheck(window, this, kWindowSyncInitTask, request_timeout_));
}

void RateLimitConnection::SendReprot(RateLimitWindow* window) {
  metric::v2::RateLimitRequest request;
  request.set_cmd(metric::v2::ACQUIRE);
  metric::v2::RateLimitReportRequest* report_request = request.mutable_ratelimitreportrequest();
  window->GetReprotRequest(report_request);
  report_request->set_clientkey(client_key_);
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    POLARIS_LOG(LOG_TRACE, "window report with request: %s", request.ShortDebugString().c_str());
  }
  stream_->SendMessage(request, false);
  // 设置超时检查
  report_task_map_[window].task_iter_ = reactor_.AddTimingTask(
      new WindowSyncTimeoutCheck(window, this, kWindowSyncReportTask, request_timeout_));
}

void RateLimitConnection::OnInitResponse(const metric::v2::RateLimitInitResponse& response) {
  uint64_t delay = Time::GetCurrentTimeMs() + request_timeout_;
  // 查找window
  LimitTargetKey target_key;
  const metric::v2::LimitTarget& limit_target = response.target();
  target_key.service_key_.namespace_          = limit_target.namespace_();
  target_key.service_key_.name_               = limit_target.service();
  target_key.labels_                          = limit_target.labels();
  std::map<LimitTargetKey, RateLimitWindow*>::iterator target_it =
      limit_target_map_.find(target_key);
  if (target_it != limit_target_map_.end()) {
    RateLimitWindow* window                                      = target_it->second;
    std::map<RateLimitWindow*, TimingTaskIter>::iterator task_it = init_task_map_.find(window);
    if (task_it != init_task_map_.end() && task_it->second != reactor_.TimingTaskEnd()) {
      if (delay > task_it->second->first) {
        delay = delay - task_it->second->first;
      } else {
        delay = 0;
      }
      reactor_.CancelTimingTask(task_it->second);  // 取消超时检查
      task_it->second = reactor_.TimingTaskEnd();
      init_task_map_.erase(task_it);

      client_key_                   = response.clientkey();
      WindowReportInfo& report_info = report_task_map_[window];
      for (int i = 0; i < response.counters_size(); ++i) {  // 处理索引
        counter_key_map_[response.counters(i).counterkey()] = window;
        report_info.counter_keys_.push_back(response.counters(i).counterkey());
      }
      report_task_map_[window].task_iter_ = reactor_.TimingTaskEnd();  // 从init迁移到同步任务
      limit_target_map_.erase(target_key);
      window->OnInitResponse(response, time_diff_);

      connector_.UpdateCallResult(cluster_, instance_id_, delay, kServerCodeReturnOk);
      reactor_.AddTimingTask(new WindowSyncTask(
          window, &connector_,
          window->GetRateLimitRule()->GetRateLimitReport().IntervalWithJitter()));
      return;
    }
    limit_target_map_.erase(target_key);  // 窗口已经不再init流程中，删除对应的数据
  }
  // 请求已经超时了
  POLARIS_LOG(LOG_WARN, "init response for target %s with timeout",
              limit_target.ShortDebugString().c_str());
}

void RateLimitConnection::OnReportResponse(const metric::v2::RateLimitReportResponse& response) {
  uint64_t delay = Time::GetCurrentTimeMs() + request_timeout_;
  if (response.quotalefts().empty()) {
    POLARIS_LOG(LOG_TRACE, "report with empty quota left response: %s",
                response.ShortDebugString().c_str());
    return;
  }
  int counter_key = response.quotalefts().begin()->counterkey();
  std::map<uint32_t, RateLimitWindow*>::iterator counter_it = counter_key_map_.find(counter_key);
  if (counter_it == counter_key_map_.end()) {
    POLARIS_LOG(LOG_TRACE, "report with counter key[%d] not exists, response: %s", counter_key,
                response.ShortDebugString().c_str());
    return;
  }
  RateLimitWindow* window                                        = counter_it->second;
  std::map<RateLimitWindow*, WindowReportInfo>::iterator task_it = report_task_map_.find(window);
  if (task_it != report_task_map_.end()) {
    if (task_it->second.task_iter_ != reactor_.TimingTaskEnd()) {
      if (delay > task_it->second.task_iter_->first) {
        delay = delay - task_it->second.task_iter_->first;
      } else {
        delay = 0;
      }
      reactor_.CancelTimingTask(task_it->second.task_iter_);  // 取消超时检查
      task_it->second.task_iter_ = reactor_.TimingTaskEnd();
      connector_.UpdateCallResult(cluster_, instance_id_, delay, kServerCodeReturnOk);
      uint64_t report_time = window->OnReportResponse(response, time_diff_);
      report_time          = report_time > delay ? report_time - delay : 0;
      reactor_.AddTimingTask(new WindowSyncTask(window, &connector_, report_time));
    } else {  // 服务器主动推送的消息
      if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
        POLARIS_LOG(LOG_TRACE, "push response: %s", response.ShortDebugString().c_str());
      }
      window->OnReportResponse(response, time_diff_);
    }
    return;
  }
  // 请求已经超时了
  POLARIS_LOG(LOG_WARN, "window for counter key[%d] not exits", counter_key);
}

void RateLimitConnection::OnResponseTimeout(RateLimitWindow* window, WindowSyncTaskType task_type) {
  if (task_type == kWindowSyncInitTask) {
    POLARIS_LOG(LOG_WARN, "init response for window %s with timeout",
                window->GetMetricId().c_str());
    init_task_map_[window] = reactor_.TimingTaskEnd();
  } else {
    POLARIS_LOG(LOG_WARN, "report response for window %s with timeout",
                window->GetMetricId().c_str());
    report_task_map_.erase(window);  // 移动到init task重新触发init
    init_task_map_[window] = reactor_.TimingTaskEnd();
  }
  reactor_.SubmitTask(new WindowSyncTask(window, &connector_));
  connector_.UpdateCallResult(cluster_, instance_id_, request_timeout_, kServerCodeRpcTimeout);
  // 连接上的请求全部超时，切换连接
  if (last_response_time_ + request_timeout_ < Time::GetCurrentTimeMs()) {
    this->CloseForError();
  }
}

void RateLimitConnection::CloseForError() {
  if (is_closing_) {
    return;  // 说明已经被别的回调触发了连接关闭
  }
  is_closing_    = true;
  uint64_t delay = Time::GetCurrentTimeMs() - last_response_time_;
  connector_.UpdateCallResult(cluster_, instance_id_, delay, kServerCodeServerError);
  TimingTaskIter timeout_end = reactor_.TimingTaskEnd();
  std::map<RateLimitWindow*, TimingTaskIter>::iterator task_it;
  if (stream_ == NULL) {  // 连接失败的情况
    for (std::map<RateLimitWindow*, TimingTaskIter>::iterator it = init_task_map_.begin();
         it != init_task_map_.end(); ++it) {
      reactor_.AddTimingTask(new WindowSyncTask(it->first, &connector_, 200));
    }
  } else {  // 连接超时
    for (std::map<RateLimitWindow*, TimingTaskIter>::iterator it = init_task_map_.begin();
         it != init_task_map_.end(); ++it) {
      if (it->second != timeout_end) {
        reactor_.CancelTimingTask(it->second);
        it->second = timeout_end;
        reactor_.AddTimingTask(new WindowSyncTask(it->first, &connector_, 200));
      }
    }
    for (std::map<RateLimitWindow*, WindowReportInfo>::iterator it = report_task_map_.begin();
         it != report_task_map_.end(); ++it) {
      if (it->second.task_iter_ != timeout_end) {
        reactor_.CancelTimingTask(it->second.task_iter_);
        it->second.task_iter_ = timeout_end;
        reactor_.AddTimingTask(new WindowSyncTask(it->first, &connector_, 200));
      }
    }
  }
  connector_.EraseConnection(this->GetId());
  // 用于异步释放连接，不能在grpc相关回调中直接释放grpc client
  this->client_->CloseStream();
  ClearTaskAndWindow();
  reactor_.SubmitTask(new DeferReleaseTask<RateLimitConnection>(this));
}

///////////////////////////////////////////////////////////////////////////////

RateLimitConnector::RateLimitConnector(Reactor& reactor, Context* context, uint64_t message_timeout)
    : reactor_(reactor), context_(context), idle_check_interval_(10 * 1000),
      remove_after_idle_time_(60 * 1000), message_timeout_(message_timeout) {
  // 提交定期空闲检查的任务
  reactor_.AddTimingTask(
      new TimingFuncTask<RateLimitConnector>(ConnectionIdleCheck, this, idle_check_interval_));
}

RateLimitConnector::~RateLimitConnector() {
  context_ = NULL;
  for (std::map<std::string, RateLimitConnection*>::iterator it = connection_mgr_.begin();
       it != connection_mgr_.end(); ++it) {
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

  RateLimitConnection* connection = NULL;
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

void RateLimitConnector::ConnectionIdleCheck(RateLimitConnector* connector) {
  uint64_t idle_check_time = Time::GetCurrentTimeMs() - connector->remove_after_idle_time_;
  for (std::map<std::string, RateLimitConnection*>::iterator it =
           connector->connection_mgr_.begin();
       it != connector->connection_mgr_.end();) {
    if (it->second->IsIdle(idle_check_time)) {
      POLARIS_LOG(LOG_DEBUG, "free idle rate limit connection: %s", it->second->GetId().c_str());
      delete it->second;  // 过期释放连接
      connector->connection_mgr_.erase(it++);
    } else {
      ++it;
    }
  }
  // 提交下次检查的定时任务
  connector->reactor_.AddTimingTask(new TimingFuncTask<RateLimitConnector>(
      ConnectionIdleCheck, connector, connector->idle_check_interval_));
}

ReturnCode RateLimitConnector::InitService(const ServiceKey& service_key) {
  rate_limit_service_ = service_key;
  ReturnCode ret_code = kReturnOk;
  if (context_->GetContextMode() == kLimitContext) {  // 只有在LimitContext模式下才立刻请求服务
    Instance* instance = NULL;
    uint64_t timeout   = context_->GetContextImpl()->GetApiDefaultTimeout();
    Criteria criteria;
    ReturnCode ret_code = ConsumerApiImpl::GetSystemServer(context_, rate_limit_service_, criteria,
                                                           instance, timeout);
    if (ret_code == kReturnOk) {
      delete instance;
    } else {
      POLARIS_LOG(LOG_ERROR, "init rate limit service[%s/%s] with error:%s",
                  rate_limit_service_.namespace_.c_str(), rate_limit_service_.name_.c_str(),
                  ReturnCodeToMsg(ret_code).c_str());
    }
  }
  return ret_code;
}

const std::string& RateLimitConnector::GetContextId() {
  return context_->GetContextImpl()->GetSdkToken().uid();
}

ReturnCode RateLimitConnector::SelectInstance(const ServiceKey& metric_cluster,
                                              const std::string& hash_key, Instance** instance) {
  // 通过一致性hash获取一个实例
  Criteria criteria;
  criteria.hash_string_ = hash_key;
  return ConsumerApiImpl::GetSystemServer(context_, metric_cluster, criteria, *instance, 0);
}

ReturnCode RateLimitConnector::SelectConnection(const ServiceKey& metric_cluster,
                                                const std::string& metric_id,
                                                RateLimitConnection*& connection) {
  Instance* instance        = NULL;  // 通过一致性hash获取一个实例
  const ServiceKey& cluster = metric_cluster.name_.empty() ? rate_limit_service_ : metric_cluster;
  ReturnCode ret_code       = SelectInstance(cluster, metric_id, &instance);
  if (ret_code != kReturnOk) {
    return ret_code;
  }
  POLARIS_LOG(LOG_DEBUG, "select service[%s/%s] instance[%s:%d] for metric:%s",
              cluster.namespace_.c_str(), cluster.name_.c_str(), instance->GetHost().c_str(),
              instance->GetPort(), metric_id.c_str());
  std::string id = instance->GetHost() + ":" + StringUtils::TypeToStr(instance->GetPort());
  std::map<std::string, RateLimitConnection*>::iterator it = connection_mgr_.find(id);
  if (it != connection_mgr_.end()) {  // 连接已存在，直接返回
    connection = it->second;
  } else {
    connection = new RateLimitConnection(*this, message_timeout_, *instance, cluster, id);
    connection_mgr_[connection->GetId()] = connection;
  }
  delete instance;
  return kReturnOk;
}

void RateLimitConnector::UpdateCallResult(const ServiceKey& service_key,
                                          const std::string& instance_id, uint64_t delay,
                                          PolarisServerCode server_code) {
  InstanceGauge instance_gauge;
  instance_gauge.service_namespace = service_key.namespace_;
  instance_gauge.service_name      = service_key.name_;
  instance_gauge.instance_id       = instance_id;
  instance_gauge.call_daley        = delay;
  instance_gauge.call_ret_code     = server_code;
  instance_gauge.call_ret_status   = kCallRetOk;  // 该服务不需要熔断
  ConsumerApiImpl::UpdateServiceCallResult(context_, instance_gauge);
}

}  // namespace polaris
