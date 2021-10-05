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

#ifndef POLARIS_CPP_POLARIS_QUOTA_RATE_LIMIT_CONNECTOR_H_
#define POLARIS_CPP_POLARIS_QUOTA_RATE_LIMIT_CONNECTOR_H_

#include <stdint.h>
#include <v2/ratelimit_v2.pb.h>

#include <map>
#include <string>

#include "grpc/client.h"
#include "grpc/status.h"
#include "model/return_code.h"
#include "polaris/defs.h"
#include "reactor/task.h"

namespace polaris {

class Context;
class Instance;
class RateLimitWindow;
class Reactor;
class RateLimitConnector;
class RateLimitConnection;

enum WindowSyncTaskType {
  kWindowSyncInitTask,    // Init任务
  kWindowSyncReportTask,  // Reprot任务
};

// 定时同步任务
class WindowSyncTask : public TimingTask {
public:
  WindowSyncTask(RateLimitWindow* window, RateLimitConnector* connector, uint64_t timeout = 0);

  ~WindowSyncTask();

  virtual void Run();

private:
  RateLimitWindow* window_;
  RateLimitConnector* connector_;
};

// 同步任务超时检查
class WindowSyncTimeoutCheck : public TimingTask {
public:
  WindowSyncTimeoutCheck(RateLimitWindow* window, RateLimitConnection* connection,
                         WindowSyncTaskType task_type, uint64_t timeout)
      : TimingTask(timeout), window_(window), connection_(connection), task_type_(task_type) {}

  virtual ~WindowSyncTimeoutCheck() {}

  virtual void Run();

private:
  RateLimitWindow* window_;
  RateLimitConnection* connection_;
  WindowSyncTaskType task_type_;
};

struct LimitTargetKey {
  ServiceKey service_key_;
  std::string labels_;
};

bool operator<(const LimitTargetKey& lhs, const LimitTargetKey& rhs);

struct WindowReportInfo {
  TimingTaskIter task_iter_;
  std::vector<uint32_t> counter_keys_;
};

// 通过一致性hash方式选择的限流Server并建立连接，并管理连接上的请求
class RateLimitConnection : public grpc::RequestCallback<metric::v2::TimeAdjustResponse>,
                            public grpc::StreamCallback<metric::v2::RateLimitResponse> {
public:
  RateLimitConnection(RateLimitConnector& connector, const uint64_t& request_timeout,
                      Instance* instance, const ServiceKey& cluster, const std::string& id);
  virtual ~RateLimitConnection();

  // 提供给连接回调对象使用
  void OnConnectSuccess();
  void OnConnectFailed();
  void OnConnectTimeout();

  // Unary 请求回调
  virtual void OnSuccess(metric::v2::TimeAdjustResponse* response);
  virtual void OnFailure(grpc::GrpcStatusCode status, const std::string& message);

  // Stream 请求回调
  virtual void OnReceiveMessage(metric::v2::RateLimitResponse* response);
  virtual void OnRemoteClose(grpc::GrpcStatusCode status, const std::string& message);

  void DoSyncTask(RateLimitWindow* window);
  void RemoveWindow(RateLimitWindow* window);

  void DoSyncTimeTask();
  void OnSyncTimeTimeout();

  void OnInitResponse(const metric::v2::RateLimitInitResponse& response);
  void OnReportResponse(const metric::v2::RateLimitReportResponse& response);
  void OnResponseTimeout(RateLimitWindow* window, WindowSyncTaskType task_type);

  // 检查连接是否空闲
  bool IsIdle(uint64_t idle_check_time) const { return last_used_time_ < idle_check_time; }

  // 获取建立连接所用的服务实例ID，标示连接
  const std::string& GetId() const { return connection_id_; }

private:
  // 请求出错关闭连接
  void CloseForError();

  void ClearTaskAndWindow();

  void SendInit(RateLimitWindow* window);

  void SendReprot(RateLimitWindow* window);

private:
  RateLimitConnector& connector_;
  Reactor& reactor_;
  const uint64_t& request_timeout_;
  ServiceKey cluster_;
  Instance* instance_;
  std::string connection_id_;
  grpc::GrpcClient* client_;
  grpc::GrpcStream* stream_;
  uint64_t last_used_time_;      // 记录上一次发送请求的时间
  uint64_t last_response_time_;  // 记录上一次请求应答的时间
  bool is_closing_;              // 记录是否提交了异步关闭任务

  TimingTaskIter sync_time_task_;  // 同步时间定时任务
  int64_t time_diff_;

  uint32_t client_key_;
  std::map<LimitTargetKey, RateLimitWindow*> limit_target_map_;
  std::map<RateLimitWindow*, TimingTaskIter> init_task_map_;
  std::map<uint32_t, RateLimitWindow*> counter_key_map_;  // 同步结果映射到window索引
  std::map<RateLimitWindow*, WindowReportInfo> report_task_map_;
};

// 同步时间定时任务/超时检查任务
enum TimeSyncTaskType {
  kTimeSyncTaskTiming,       // 定时同步任务
  kTimeSyncTaskTimoutCheck,  // 定时同步任务超时检查
};

class TimeSyncTask : public TimingTask {
public:
  TimeSyncTask(RateLimitConnection* connection, TimeSyncTaskType task_type, uint64_t timeout)
      : TimingTask(timeout), task_type_(task_type), connection_(connection) {}

  virtual ~TimeSyncTask() {}

  virtual void Run() {
    if (task_type_ == kTimeSyncTaskTiming) {
      connection_->DoSyncTimeTask();
    } else {
      connection_->OnSyncTimeTimeout();
    }
  }

private:
  TimeSyncTaskType task_type_;
  RateLimitConnection* connection_;
};

/// @brief 负责与限流服务器同步限流数据
class RateLimitConnector {
public:
  RateLimitConnector(Reactor& reactor, Context* context, uint64_t message_timeout);

  virtual ~RateLimitConnector();

  ReturnCode InitService(const ServiceKey& service_key);

  void SyncTask(RateLimitWindow* window);

  // 定时检查空闲连接
  static void ConnectionIdleCheck(RateLimitConnector* connector);

  Reactor& GetReactor() { return reactor_; }

  void UpdateCallResult(const ServiceKey& cluster, Instance* instance, uint64_t delay,
                        PolarisServerCode server_code);

  void EraseConnection(const std::string& connection_id) { connection_mgr_.erase(connection_id); }

  uint64_t GetMessageTimeout() { return message_timeout_; }

  const std::string& GetContextId();

private:
  ReturnCode SelectConnection(const ServiceKey& metric_cluster, const std::string& metric_id,
                              RateLimitConnection*& connection);

private:
  Reactor& reactor_;
  Context* context_;
  uint64_t idle_check_interval_;
  uint64_t remove_after_idle_time_;
  ServiceKey rate_limit_service_;
  uint64_t message_timeout_;

protected:  // protected for test
  virtual ReturnCode SelectInstance(const ServiceKey& metric_cluster, const std::string& hash_key,
                                    Instance** instance);

  // 通过限流服务实例ID索引的限流连接
  std::map<std::string, RateLimitConnection*> connection_mgr_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_RATE_LIMIT_CONNECTOR_H_
