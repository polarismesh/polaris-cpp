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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_SERVER_CONNECTOR_SERVER_CONNECTOR_H_
#define POLARIS_CPP_POLARIS_PLUGIN_SERVER_CONNECTOR_SERVER_CONNECTOR_H_

#include <pthread.h>
#include <stdint.h>

#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "config/seed_server.h"
#include "grpc/client.h"
#include "grpc/status.h"
#include "model/model_impl.h"
#include "model/return_code.h"
#include "plugin/server_connector/timeout_strategy.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "reactor/reactor.h"
#include "reactor/task.h"
#include "sync/future.h"
#include "v1/response.pb.h"

namespace polaris {

class GrpcServerConnector;

struct ServiceListener {
  ServiceKeyWithType service_;
  uint64_t sync_interval_;
  ServiceEventHandler* handler_;  // 时间监听回调
  std::string revision_;          // Server返回的服务数据的版本号
  uint64_t cache_version_;        // 递增的整型缓存版本号
  uint32_t ret_code_;             // 记录上一次请求的code
  TimingTaskIter discover_task_iter_;  // 记录定时服务发现任务，服务过期时用于删除任务
  TimingTaskIter timeout_task_iter_;  // 记录服务发现超时检查任务，用于删除
  GrpcServerConnector* connector_;
};

// 封装监听事件
class DiscoverEventTask : public Task {
public:
  explicit DiscoverEventTask(GrpcServerConnector* connector, const ServiceKey& service_key,
                             ServiceDataType data_type, uint64_t sync_interval,
                             ServiceEventHandler* handler);

  virtual ~DiscoverEventTask();

  virtual void Run();

private:
  friend class GrpcServerConnector;
  GrpcServerConnector* connector_;
  ServiceKeyWithType service_;
  uint64_t sync_interval_;
  ServiceEventHandler* handler_;  // null for deregistry
};

// 标示Discover服务连接的状态
enum DiscoverStreamState {
  kDiscoverStreamNotInit     = 0,
  kDiscoverStreamGetInstance = 1,
  kDiscoverStreamInit,
};

// 标示服务切换状态
enum ServerSwitchState {
  kServerSwitchInit,   // 初始化，等待发起第一次服务连接
  kServerSwitchBegin,  // 切换新服务器异步发起连接成功，添加连接超时检查任务
  kServerSwitchTimeout,   // 连接检查任务发现新连接建立超时，切换新服务重试
  kServerSwitchNormal,    // 服务连接正常，等待下一个周期正常切换
  kServerSwitchDefault,   // 埋点服务获取正常，触发切换
  kServerSwitchPeriodic,  // 定期触发切换
};

enum BlockRequestType {
  kBlockRequestInit,
  kBlockRegisterInstance,
  kBlockDeregisterInstance,
  kBlockHeartbeat,
  kBlockReportClient,
};

class BlockRequest;
class AsyncRequest;

/// @brief GRPC连接Server
class GrpcServerConnector : public ServerConnector,
                            public grpc::StreamCallback<v1::DiscoverResponse> {
public:
  GrpcServerConnector();

  virtual ~GrpcServerConnector();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual ReturnCode RegisterEventHandler(const ServiceKey& service_key, ServiceDataType data_type,
                                          uint64_t sync_interval, ServiceEventHandler* handler);

  virtual ReturnCode DeregisterEventHandler(const ServiceKey& service_key,
                                            ServiceDataType data_type);

  void ProcessQueuedListener(DiscoverEventTask* discover_event);

  // grpc::StreamCallback
  virtual void OnReceiveMessage(v1::DiscoverResponse* response);
  virtual void OnRemoteClose(grpc::GrpcStatusCode status, const std::string& message);

  void UpdateCallResult(PolarisServerCode server_code, uint64_t delay);

  virtual ReturnCode RegisterInstance(const InstanceRegisterRequest& req, uint64_t timeout_ms,
                                      std::string& instance_id);

  virtual ReturnCode DeregisterInstance(const InstanceDeregisterRequest& req, uint64_t timeout_ms);

  virtual ReturnCode InstanceHeartbeat(const InstanceHeartbeatRequest& req, uint64_t timeout_ms);

  virtual ReturnCode AsyncInstanceHeartbeat(const InstanceHeartbeatRequest& req,
                                            uint64_t timeout_ms, ProviderCallback* callback);

  virtual ReturnCode ReportClient(const std::string& host, uint64_t timeout_ms, Location& location);

  Reactor& GetReactor() { return reactor_; }

  bool GetInstance(BlockRequest* block_request);

  void UpdateCallResult(BlockRequest* block_request);

private:
  ReturnCode InitTimeoutStrategy(Config* config);

  void UpdateMaxUsedTime(uint64_t used_time) {
    if (used_time > message_used_time_) message_used_time_ = used_time;
  }

  // 线程启动函数
  static void* ThreadFunction(void* args);

  // 用于设置定时切换服务器，或在切换后检查切换服务器是否成功
  static void TimingServerSwitch(GrpcServerConnector* server_connector);
  // 执行切换服务器的逻辑
  void ServerSwitch();

  static void TimingDiscover(ServiceListener* service_listener);

  static void DiscoverTimoutCheck(ServiceListener* service_listener);

  bool SendDiscoverRequest(ServiceListener& service_listener);

  bool CompareVersion(ServiceListener& listener, const ::v1::DiscoverResponse& response);

  ReturnCode ProcessDiscoverResponse(::v1::DiscoverResponse& response);

protected:  // for test
  virtual BlockRequest* CreateBlockRequest(BlockRequestType request_type, uint64_t timeout);

  DiscoverStreamState discover_stream_state_;
  virtual ReturnCode SelectInstance(const ServiceKey& service_key, uint32_t timeout,
                                    Instance** instance, bool ignore_half_open = false);

private:
  friend class DiscoverConnectionCb;
  friend class AsyncRequest;
  Context* context_;
  std::vector<SeedServer> server_lists_;
  pthread_t task_thread_id_;
  Reactor reactor_;
  std::string discover_instance_;
  grpc::GrpcClient* grpc_client_;
  grpc::GrpcStream* discover_stream_;
  uint64_t stream_response_time_;
  std::set<ServiceListener*> pending_for_connected_;

  uint64_t server_switch_interval_;
  ServerSwitchState server_switch_state_;  // 维护服务切换状态
  TimingTaskIter server_switch_task_iter_;

  TimeoutStrategy connect_timeout_;
  TimeoutStrategy message_timeout_;
  uint64_t message_used_time_;  // stream上请求最大耗时
  std::size_t request_queue_size_;

  uint64_t last_cache_version_;

  std::map<ServiceKeyWithType, ServiceListener> listener_map_;

  std::map<uint64_t, AsyncRequest*> async_request_map_;
};

class DiscoverConnectionCb : public grpc::ConnectCallback {
public:
  explicit DiscoverConnectionCb(GrpcServerConnector* connector);
  virtual ~DiscoverConnectionCb();

  virtual void OnSuccess();
  virtual void OnFailed();
  virtual void OnTimeout();

private:
  GrpcServerConnector* connector_;
  uint64_t connect_time_;
};

class BlockRequest : public grpc::RequestCallback<v1::Response> {
public:
  BlockRequest(BlockRequestType request_type, GrpcServerConnector& connector,
               uint64_t request_timeout);

  virtual ~BlockRequest();

  const char* GetCallPath();

  const char* RequestTypeToStr();

  virtual void OnSuccess(::v1::Response* response);

  virtual void OnFailure(grpc::GrpcStatusCode status, const std::string& message);

  // 建立连接成功，但不加入reactor的客户端
  virtual bool PrepareClient();

  uint64_t GetTimeout() { return request_timeout_; }

  Future<v1::Response>* SendRequest(google::protobuf::Message* message);

private:
  friend class GrpcServerConnector;
  friend class BlockRequestTask;
  friend class BlockRequestTimeout;

  BlockRequestType request_type_;
  GrpcServerConnector& connector_;
  uint64_t request_timeout_;
  PolarisServerCode server_code_;
  uint64_t call_begin_;
  google::protobuf::Message* message_;
  Promise<v1::Response>* promise_;

protected:  // protected for test
  Instance* instance_;
  grpc::GrpcClient* grpc_client_;
};

// 请求执行任务
class BlockRequestTask : public Task {
public:
  explicit BlockRequestTask(BlockRequest* request);
  virtual ~BlockRequestTask();

  virtual void Run();

private:
  BlockRequest* request_;
};

// 超时检查
class BlockRequestTimeout : public TimingTask {
public:
  BlockRequestTimeout(BlockRequest* request, uint64_t timeout);
  virtual ~BlockRequestTimeout();

  virtual void Run();

private:
  BlockRequest* request_;
};

class AsyncRequest : public grpc::RequestCallback<v1::Response> {
public:
  AsyncRequest(Reactor& reactor, GrpcServerConnector* connector, uint64_t request_id,
               v1::Instance* request, uint64_t timeout, ProviderCallback* callback);

  ~AsyncRequest();

  bool Submit();

  // 回调函数，提供给连接回调对象使用
  void OnConnectSuccess();
  void OnConnectFailed();
  void OnConnectTimeout();

  // grpc::RequestCallback
  virtual void OnSuccess(::v1::Response* response);
  virtual void OnFailure(grpc::GrpcStatusCode status, const std::string& message);

  static void RequsetTimeoutCheck(AsyncRequest* request);

  uint64_t GetTimeLeft() const;  // 计算剩余超时时间

  bool CheckServiceReady();  // 检查相关服务是否就绪

  ProviderCallback* GetCallback() const { return callback_; }

private:
  void Complete(PolarisServerCode server_code);

private:
  Reactor& reactor_;
  GrpcServerConnector* connector_;
  uint64_t request_id_;
  v1::Instance* request_;
  uint64_t begin_time_;
  uint64_t timeout_;
  ProviderCallback* callback_;
  Instance* server_;  // 选择连接的服务器
  grpc::GrpcClient* client_;
  TimingTaskIter timing_task_;
};

// 提交异步任务到reactor
class AsyncRequestSubmit : public TimingTask {
public:
  explicit AsyncRequestSubmit(AsyncRequest* request)
      : TimingTask(10), request_(request), next_time_(10) {}

  virtual ~AsyncRequestSubmit();

  virtual void Run();

  virtual uint64_t NextRunTime();

private:
  AsyncRequest* request_;
  uint64_t next_time_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_SERVER_CONNECTOR_SERVER_CONNECTOR_H_
