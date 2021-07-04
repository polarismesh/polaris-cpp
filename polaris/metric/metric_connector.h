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

#ifndef POLARIS_CPP_POLARIS_METRIC_METRIC_CONNECTOR_H_
#define POLARIS_CPP_POLARIS_METRIC_METRIC_CONNECTOR_H_

#include <stddef.h>
#include <stdint.h>
#include <v1/metric.pb.h>

#include <map>
#include <string>

#include "grpc/grpc_client.h"
#include "grpc/status.h"
#include "metric/metric_key_wrapper.h"
#include "model/return_code.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "reactor/task.h"

namespace polaris {

class Context;
class MetricConnection;
class MetricConnector;
class Reactor;

// Metric请求类型
enum MetricRpcType {
  kMetricRpcTypeInit,    // 初始化Metric
  kMetricRpcTypeQuery,   // 查询Metric
  kMetricRpcTypeReport,  // 定期同步Metric
};

// Metric请求状态
enum MetricRequestStatus {
  kMetricRequestStatusNone,      // 未设置
  kMetricRequestStatusPending,   // 待发送
  kMetricRequestStatusInflight,  // 发送待应答
};

// 异步初始化请求或同步请求
struct MetricInflightRequest {
  MetricInflightRequest(MetricRpcType rpc_type, grpc::RpcCallback<v1::MetricResponse>* callback,
                        uint64_t timeout);
  ~MetricInflightRequest();

  const v1::MetricKey* GetMetricKey() {
    switch (rpc_type_) {
      case kMetricRpcTypeInit:
        return &request_.init_->key();
      case kMetricRpcTypeQuery:
        return &request_.report_->key();
      case kMetricRpcTypeReport:
        return &request_.query_->key();
    }
    return NULL;
  }

  union Request {  // 用于pending状态下缓存请求
    v1::MetricInitRequest* init_;
    v1::MetricQueryRequest* query_;
    v1::MetricRequest* report_;
  };

  MetricRequestStatus status_;                       // 请求状态
  MetricRpcType rpc_type_;                           // 请求类型
  Request request_;                                  // 请求
  uint64_t timeout_;                                 // 超时时间
  grpc::RpcCallback<v1::MetricResponse>* callback_;  // 请求回调
  TimingTaskIter timeout_iter_;                      // 超时检查任务指针
};

class MetricConnection;
// 请求超时检查任务
class MetricRequestTimeoutCheck : public TimingTask {
public:
  MetricRequestTimeoutCheck(uint64_t msg_id, MetricConnection* connection, uint64_t timeout)
      : TimingTask(timeout), msg_id_(msg_id), connection_(connection) {}

  virtual ~MetricRequestTimeoutCheck() { connection_ = NULL; }

  virtual void Run();

private:
  uint64_t msg_id_;  // 用于索引请求
  MetricConnection* connection_;
};

class MetricConnector;
// 通过一致性hash方式选择的限流Server并建立连接，并管理连接上的请求
class MetricConnection : public grpc::RequestCallback<v1::MetricResponse>,
                         public grpc::StreamCallback<v1::MetricResponse> {
public:
  MetricConnection(MetricConnector* metric_connector, Instance* instance);
  virtual ~MetricConnection();

  // 回调函数，提供给连接回调对象使用
  void OnConnectSuccess();
  void OnConnectFailed();
  void OnConnectTimeout();

  // Unary 请求回调
  virtual void OnSuccess(v1::MetricResponse* response);
  virtual void OnFailure(grpc::GrpcStatusCode status, const std::string& message);

  // Stream 请求回调
  virtual void OnReceiveMessage(v1::MetricResponse* response);
  virtual void OnRemoteClose(grpc::GrpcStatusCode status, const std::string& message);

  // 异步发送初始化请求
  // 如果连接还未建立，则缓存请求，等连接建立后自动发送
  void SendInitRequest(v1::MetricInitRequest* request, uint64_t timeout,
                       grpc::RpcCallback<v1::MetricResponse>* callback);

  // 发送查询请求
  // 如果连接还未建立，则缓存请求
  void SendQueryStream(v1::MetricQueryRequest* request, uint64_t timeout,
                       grpc::RpcCallback<v1::MetricResponse>* callback);

  // 发送上报请求
  // 如果连接未建立，则直接报错，发送Report请求前必须确保Init请求已经应答
  void SendReportStream(v1::MetricRequest* request, uint64_t timeout,
                        grpc::RpcCallback<v1::MetricResponse>* callback);

  // 检查连接是否空闲
  bool CheckIdle(uint64_t idle_check_time);

  // 检查metric key是否成功init过
  bool IsMetricInit(v1::MetricKey* metric_key);

  // 获取建立连接所用的服务实例ID，标示连接
  const std::string& GetId() { return instance_->GetId(); }

private:
  // 根据请求类型获取RPC请求路径
  static const char* GetCallPath(MetricRpcType rate_limit_rpc_type);

  // 请求出错关闭连接
  void CloseForError();

  void ResponseErrHandler(uint32_t resp_code,
                          std::map<uint64_t, MetricInflightRequest*>::iterator& it);

private:
  friend class MetricRequestTimeoutCheck;
  MetricConnector* connector_;
  Instance* instance_;
  grpc::GrpcClient* client_;
  grpc::GrpcStream* query_stream_;
  grpc::GrpcStream* report_stream_;

  // 通过请求ID索引inflight requests
  std::map<uint64_t, MetricInflightRequest*> inflight_map_;
  // 通过请求Key索引最后一次使用时间，并用于确定是否Init
  std::map<MetricKeyWrapper, uint64_t> metric_key_init_;

  uint64_t last_used_time_;  // 记录上一次发送请求的时间
  bool is_closing_;          // 记录是否提交了异步关闭任务
};

// 负责与限流服务器同步限流数据
class MetricConnector {
public:
  MetricConnector(Reactor& reactor, Context* context);

  virtual ~MetricConnector();

  // Metric是否已经初始化
  virtual bool IsMetricInit(v1::MetricKey* metric_key);

  // Metric初始化请求
  virtual ReturnCode Initialize(v1::MetricInitRequest* request, uint64_t timeout,
                                grpc::RpcCallback<v1::MetricResponse>* callback);

  // Metric查询请求
  virtual ReturnCode Query(v1::MetricQueryRequest* request, uint64_t timeout,
                           grpc::RpcCallback<v1::MetricResponse>* callback);

  // Metric上报请求
  virtual ReturnCode Report(v1::MetricRequest* request, uint64_t timeout,
                            grpc::RpcCallback<v1::MetricResponse>* callback);

  Reactor& GetReactor() { return reactor_; }

  // 提供MetricConnection回调
  void UpdateCallResult(Instance* instance, PolarisServerCode server_code);
  void EraseConnection(const std::string& key) { connection_mgr_.erase(key); }

private:
  ReturnCode SelectConnection(const v1::MetricKey& metric_key, MetricConnection*& connection);

  uint64_t NextMsgId();

private:
  Reactor& reactor_;
  Context* context_;
  uint64_t idle_check_interval_;     // 空闲连接检查间隔
  uint64_t remove_after_idle_time_;  // 连接空闲删除时间

protected:  // protected for test
  virtual ReturnCode SelectInstance(const std::string& hash_key, Instance** instance);

  // 定时检查空闲连接
  static void ConnectionIdleCheck(MetricConnector* connector);

  // 通过限流服务实例ID索引的限流连接
  std::map<std::string, MetricConnection*> connection_mgr_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_METRIC_METRIC_CONNECTOR_H_
