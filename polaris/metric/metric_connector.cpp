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

#include "metric/metric_connector.h"

#include <google/protobuf/wrappers.pb.h>
#include <inttypes.h>
#include <stdint.h>
#include <v1/code.pb.h>
#include <v1/metric.pb.h>

#include <utility>

#include "api/consumer_api.h"
#include "config/seed_server.h"
#include "context_internal.h"
#include "logger.h"
#include "polaris/context.h"
#include "polaris/log.h"
#include "polaris/plugin.h"
#include "reactor/reactor.h"
#include "utils/time_clock.h"

namespace polaris {

MetricInflightRequest::MetricInflightRequest(MetricRpcType rpc_type,
                                             grpc::RpcCallback<v1::MetricResponse>* callback,
                                             uint64_t timeout)
    : status_(kMetricRequestStatusNone), rpc_type_(rpc_type), timeout_(timeout),
      callback_(callback) {}

MetricInflightRequest::~MetricInflightRequest() {
  delete callback_;
  if (status_ == kMetricRequestStatusPending) {
    if (rpc_type_ == kMetricRpcTypeInit) {
      delete request_.init_;
    } else if (rpc_type_ == kMetricRpcTypeQuery) {
      delete request_.query_;
    } else if (rpc_type_ == kMetricRpcTypeReport) {
      delete request_.report_;
    }
  } else if (status_ == kMetricRequestStatusInflight) {
    if (rpc_type_ == kMetricRpcTypeInit) {
      delete request_.init_;
    }
  }
}

void MetricRequestTimeoutCheck::Run() {
  // 如果能够触发超时回调，那么请求一定还在请求列表中，从请求列表中删除
  std::map<uint64_t, MetricInflightRequest*>::iterator it =
      connection_->inflight_map_.find(msg_id_);
  POLARIS_ASSERT(it != connection_->inflight_map_.end());
  it->second->callback_->OnError(kReturnTimeout);
  delete it->second;
  connection_->inflight_map_.erase(it);
  // FIXME(lambdaliu): 一个Stream上请求很多时Server容易超时
  // 一个请求超时，所有请求都算失败
  connection_->CloseForError();
}

MetricConnection::MetricConnection(MetricConnector* metric_connector, Instance* instance)
    : connector_(metric_connector), instance_(instance) {
  client_        = new grpc::GrpcClient(connector_->GetReactor());
  query_stream_  = NULL;
  report_stream_ = NULL;
  // 发起异步请求
  client_->ConnectTo(instance_->GetHost(), instance_->GetPort(), 1000,
                     new grpc::ConnectCallbackRef<MetricConnection>(*this));
  last_used_time_ = Time::GetCurrentTimeMs();
  is_closing_     = false;
}

MetricConnection::~MetricConnection() {
  connector_ = NULL;
  if (instance_ != NULL) {
    delete instance_;
    instance_ = NULL;
  }
  query_stream_  = NULL;
  report_stream_ = NULL;
  if (client_ != NULL) {
    delete client_;
    client_ = NULL;
  }
  for (std::map<uint64_t, MetricInflightRequest*>::iterator it = inflight_map_.begin();
       it != inflight_map_.end(); ++it) {
    delete it->second;
  }
}

void MetricConnection::OnConnectSuccess() {
  query_stream_  = client_->StartStream(GetCallPath(kMetricRpcTypeQuery), *this);
  report_stream_ = client_->StartStream(GetCallPath(kMetricRpcTypeReport), *this);
  // 连接成功，发送所有待发送的请求
  for (std::map<uint64_t, MetricInflightRequest*>::iterator it = inflight_map_.begin();
       it != inflight_map_.end(); ++it) {
    POLARIS_ASSERT(it->second->status_ == kMetricRequestStatusPending);
    if (it->second->rpc_type_ == kMetricRpcTypeInit) {
      client_->SendRequest(*it->second->request_.init_, GetCallPath(kMetricRpcTypeInit),
                           it->second->timeout_, *this);
    } else if (it->second->rpc_type_ == kMetricRpcTypeQuery) {
      query_stream_->SendMessage(*it->second->request_.query_, false);
      delete it->second->request_.query_;
    } else if (it->second->rpc_type_ == kMetricRpcTypeReport) {
      report_stream_->SendMessage(*it->second->request_.report_, false);
      delete it->second->request_.report_;
    }
    // 设置超时检查
    it->second->status_       = kMetricRequestStatusInflight;
    it->second->timeout_iter_ = connector_->GetReactor().AddTimingTask(
        new MetricRequestTimeoutCheck(it->first, this, it->second->timeout_));
  }
  POLARIS_LOG(LOG_INFO, "metric connect to server[%s:%d] success, send %zu pending init request",
              instance_->GetHost().c_str(), instance_->GetPort(), inflight_map_.size());
}

void MetricConnection::OnConnectFailed() {
  POLARIS_LOG(LOG_ERROR, "metric connect to server[%s:%d] failed", instance_->GetHost().c_str(),
              instance_->GetPort());
  this->CloseForError();
}

void MetricConnection::OnConnectTimeout() {
  POLARIS_LOG(LOG_ERROR, "metric connect to server[%s:%d] timeout", instance_->GetHost().c_str(),
              instance_->GetPort());
  this->CloseForError();
}

void MetricConnection::OnSuccess(v1::MetricResponse* response) {
  std::map<uint64_t, MetricInflightRequest*>::iterator it =
      inflight_map_.find(response->msgid().value());
  if (it == inflight_map_.end()) {
    POLARIS_LOG(LOG_WARN, "metric request for msgid[%" PRIu64 "] not found",
                response->msgid().value());
    delete response;
    return;
  }
  connector_->UpdateCallResult(instance_, kServerCodeReturnOk);
  if (response->code().value() == v1::ExecuteContinue) {
    POLARIS_LOG(LOG_DEBUG, "metric request continue wait for msgid[%" PRIu64 "] ",
                response->msgid().value());
    delete response;
    return;
  }

  uint32_t resp_code = response->code().value();
  it->second->callback_->OnSuccess(response);
  POLARIS_ASSERT(it->second->status_ == kMetricRequestStatusInflight);
  connector_->GetReactor().CancelTimingTask(it->second->timeout_iter_);  // 取消超时检查

  if (resp_code == v1::ExecuteSuccess) {
    if (it->second->rpc_type_ == kMetricRpcTypeInit) {
      // 记录Init成功
      MetricKeyWrapper metric_key_wrapper(it->second->request_.init_->mutable_key());
      metric_key_init_[metric_key_wrapper] = Time::GetCurrentTimeMs();
    }
    delete it->second;
    inflight_map_.erase(it);
  } else {
    ResponseErrHandler(resp_code, it);
    delete it->second;
    inflight_map_.erase(it);
  }
}

void MetricConnection::ResponseErrHandler(
    uint32_t rsp_code, std::map<uint64_t, MetricInflightRequest*>::iterator& it) {
  uint32_t err_type = rsp_code / 1000;
  if (err_type == 400) {
    POLARIS_LOG(LOG_ERROR, "send metric request to server[%s:%d] with error %d",
                instance_->GetHost().c_str(), instance_->GetPort(), rsp_code);
  } else if (err_type == 404) {
    // 需要重新init
    POLARIS_LOG(LOG_INFO, "send metric request to server[%s:%d] with error %d need reInit",
                instance_->GetHost().c_str(), instance_->GetPort(), rsp_code);
    const v1::MetricKey* metric_key = it->second->GetMetricKey();
    if (metric_key != NULL) {
      v1::MetricKey* key = const_cast<v1::MetricKey*>(metric_key);
      MetricKeyWrapper metric_key_wrapper(key);
      std::map<MetricKeyWrapper, uint64_t>::iterator it = metric_key_init_.find(metric_key_wrapper);
      if (it == metric_key_init_.end()) {
        metric_key_init_.erase(it);
      }
    }
  }
}

void MetricConnection::OnFailure(grpc::GrpcStatusCode status, const std::string& message) {
  POLARIS_LOG(LOG_ERROR, "send metric request to server[%s:%d] with error %d-%s",
              instance_->GetHost().c_str(), instance_->GetPort(), status, message.c_str());
  this->CloseForError();
}

void MetricConnection::OnReceiveMessage(v1::MetricResponse* response) { this->OnSuccess(response); }

void MetricConnection::OnRemoteClose(grpc::GrpcStatusCode status, const std::string& message) {
  POLARIS_LOG(LOG_ERROR, "metric stream to server[%s:%d] closed with %d-%s",
              instance_->GetHost().c_str(), instance_->GetPort(), status, message.c_str());
  this->CloseForError();
}

void MetricConnection::SendInitRequest(v1::MetricInitRequest* request, uint64_t timeout,
                                       grpc::RpcCallback<v1::MetricResponse>* callback) {
  last_used_time_ = Time::GetCurrentTimeMs() + timeout;
  MetricInflightRequest* inflight_request =
      new MetricInflightRequest(kMetricRpcTypeInit, callback, timeout);
  inflight_map_[request->msgid().value()] = inflight_request;
  if (report_stream_ != NULL) {  // 说明已经连接成功，直接发送请求
    client_->SendRequest(*request, GetCallPath(kMetricRpcTypeInit), timeout, *this);
    // 设置超时检查
    inflight_request->timeout_iter_ = connector_->GetReactor().AddTimingTask(
        new MetricRequestTimeoutCheck(request->msgid().value(), this, timeout));
    inflight_request->status_ = kMetricRequestStatusInflight;
  } else {
    inflight_request->status_ = kMetricRequestStatusPending;
  }
  // 保存请求，连接成功再发送，应答完再删除
  inflight_request->request_.init_ = request;
}

void MetricConnection::SendQueryStream(v1::MetricQueryRequest* request, uint64_t timeout,
                                       grpc::RpcCallback<v1::MetricResponse>* callback) {
  last_used_time_ = Time::GetCurrentTimeMs() + timeout;
  MetricInflightRequest* inflight_request =
      new MetricInflightRequest(kMetricRpcTypeQuery, callback, timeout);
  inflight_map_[request->msgid().value()] = inflight_request;
  if (query_stream_ != NULL) {  // 已经建立过连接
    query_stream_->SendMessage(*request, false);
    // 设置超时检查
    inflight_request->timeout_iter_ = connector_->GetReactor().AddTimingTask(
        new MetricRequestTimeoutCheck(request->msgid().value(), this, timeout));
    delete request;
    inflight_request->status_ = kMetricRequestStatusInflight;
  } else {
    inflight_request->status_         = kMetricRequestStatusPending;
    inflight_request->request_.query_ = request;
  }
}

void MetricConnection::SendReportStream(v1::MetricRequest* request, uint64_t timeout,
                                        grpc::RpcCallback<v1::MetricResponse>* callback) {
  last_used_time_ = Time::GetCurrentTimeMs() + timeout;
  MetricInflightRequest* inflight_request =
      new MetricInflightRequest(kMetricRpcTypeReport, callback, timeout);
  inflight_map_[request->msgid().value()] = inflight_request;
  if (report_stream_ != NULL) {  // 已经建立过连接
    report_stream_->SendMessage(*request, false);
    // 设置超时检查
    inflight_request->timeout_iter_ = connector_->GetReactor().AddTimingTask(
        new MetricRequestTimeoutCheck(request->msgid().value(), this, timeout));
    delete request;
    inflight_request->status_ = kMetricRequestStatusInflight;
  } else {
    inflight_request->status_          = kMetricRequestStatusPending;
    inflight_request->request_.report_ = request;
  }
}

bool MetricConnection::CheckIdle(uint64_t idle_check_time) {
  if (inflight_map_.empty() && last_used_time_ < idle_check_time) {
    return true;
  }
  for (std::map<MetricKeyWrapper, uint64_t>::iterator it = metric_key_init_.begin();
       it != metric_key_init_.end();) {
    if (it->second < idle_check_time) {  // 太久没有请求也需要重新Init
      metric_key_init_.erase(it++);
    } else {
      it++;
    }
  }
  return false;
}

bool MetricConnection::IsMetricInit(v1::MetricKey* metric_key) {
  MetricKeyWrapper metric_key_wrapper(metric_key);
  std::map<MetricKeyWrapper, uint64_t>::iterator it = metric_key_init_.find(metric_key_wrapper);
  if (it == metric_key_init_.end()) {
    return false;
  }
  it->second = Time::GetCurrentTimeMs();
  return true;
}

const char* MetricConnection::GetCallPath(MetricRpcType metric_rpc_type) {
  switch (metric_rpc_type) {
    case kMetricRpcTypeInit:
      return "/v1.MetricGRPC/Init";
    case kMetricRpcTypeQuery:
      return "/v1.MetricGRPC/Query";
    case kMetricRpcTypeReport:
      return "/v1.MetricGRPC/Report";
    default:
      POLARIS_ASSERT(false);
  }
}

void MetricConnection::CloseForError() {
  if (is_closing_) {
    return;  // 说明已经被别的回调触发了连接关闭
  }
  is_closing_ = true;
  connector_->UpdateCallResult(instance_, kServerCodeServerError);  // 上报Instance结果
  // 触发所有请求回调失败
  for (std::map<uint64_t, MetricInflightRequest*>::iterator it = inflight_map_.begin();
       it != inflight_map_.end(); ++it) {
    if (it->second->status_ == kMetricRequestStatusInflight) {
      connector_->GetReactor().CancelTimingTask(it->second->timeout_iter_);  // 取消超时检查
    }
    it->second->callback_->OnError(kReturnNetworkFailed);
  }
  connector_->EraseConnection(this->GetId());
  // 用于异步释放连接，不能在grpc相关回调中直接释放grpc client
  this->client_->CloseStream();
  connector_->GetReactor().SubmitTask(new DeferReleaseTask<MetricConnection>(this));
}

///////////////////////////////////////////////////////////////////////////////

MetricConnector::MetricConnector(Reactor& reactor, Context* context)
    : reactor_(reactor), context_(context), idle_check_interval_(10 * 1000),
      remove_after_idle_time_(60 * 1000) {
  // 提交定期空闲检查的任务
  reactor_.AddTimingTask(
      new TimingFuncTask<MetricConnector>(ConnectionIdleCheck, this, idle_check_interval_));
}

MetricConnector::~MetricConnector() {
  context_ = NULL;
  for (std::map<std::string, MetricConnection*>::iterator it = connection_mgr_.begin();
       it != connection_mgr_.end(); ++it) {
    delete it->second;
  }
  connection_mgr_.clear();
}

bool MetricConnector::IsMetricInit(v1::MetricKey* metric_key) {
  MetricConnection* connection = NULL;
  if (SelectConnection(*metric_key, connection) != kReturnOk) {
    return false;
  }
  return connection->IsMetricInit(metric_key);
}

ReturnCode MetricConnector::Initialize(v1::MetricInitRequest* request, uint64_t timeout,
                                       grpc::RpcCallback<v1::MetricResponse>* callback) {
  MetricConnection* connection = NULL;
  ReturnCode ret_code          = SelectConnection(request->key(), connection);
  if (ret_code == kReturnOk) {
    if (!request->has_msgid()) {
      request->mutable_msgid()->set_value(NextMsgId());
    }
    if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
      POLARIS_LOG(LOG_TRACE, "send init metric request: %s", request->ShortDebugString().c_str());
    }
    connection->SendInitRequest(request, timeout, callback);
    return kReturnOk;
  } else {
    callback->OnError(ret_code);
    delete callback;
    delete request;
    return ret_code;
  }
}

ReturnCode MetricConnector::Query(v1::MetricQueryRequest* request, uint64_t timeout,
                                  grpc::RpcCallback<v1::MetricResponse>* callback) {
  MetricConnection* connection = NULL;
  ReturnCode ret_code          = SelectConnection(request->key(), connection);
  if (ret_code == kReturnOk) {
    if (!request->has_msgid()) {
      request->mutable_msgid()->set_value(NextMsgId());
    }
    if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
      POLARIS_LOG(LOG_TRACE, "send query metric request: %s", request->ShortDebugString().c_str());
    }
    connection->SendQueryStream(request, timeout, callback);
    return kReturnOk;
  } else {
    callback->OnError(ret_code);
    delete callback;
    delete request;
    return ret_code;
  }
}

ReturnCode MetricConnector::Report(v1::MetricRequest* request, uint64_t timeout,
                                   grpc::RpcCallback<v1::MetricResponse>* callback) {
  MetricConnection* connection = NULL;
  ReturnCode ret_code          = SelectConnection(request->key(), connection);
  if (ret_code == kReturnOk) {
    if (!request->has_msgid()) {
      request->mutable_msgid()->set_value(NextMsgId());
    }
    if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
      POLARIS_LOG(LOG_TRACE, "send reprot metric request: %s", request->ShortDebugString().c_str());
    }
    connection->SendReportStream(request, timeout, callback);
    return kReturnOk;
  } else {
    callback->OnError(ret_code);
    delete callback;
    delete request;
    return ret_code;
  }
}

uint64_t MetricConnector::NextMsgId() {
  static __thread uint64_t msg_id = 0;
  return msg_id++;
}

void MetricConnector::ConnectionIdleCheck(MetricConnector* connector) {
  uint64_t idle_check_time = Time::GetCurrentTimeMs() - connector->remove_after_idle_time_;
  for (std::map<std::string, MetricConnection*>::iterator it = connector->connection_mgr_.begin();
       it != connector->connection_mgr_.end();) {
    if (it->second->CheckIdle(idle_check_time)) {
      POLARIS_LOG(LOG_DEBUG, "free idle rate limit connection: %s", it->second->GetId().c_str());
      delete it->second;  // 过期释放连接
      connector->connection_mgr_.erase(it++);
    } else {
      ++it;
    }
  }
  // 提交下次检查的定时任务
  connector->reactor_.AddTimingTask(new TimingFuncTask<MetricConnector>(
      ConnectionIdleCheck, connector, connector->idle_check_interval_));
}

ReturnCode MetricConnector::SelectInstance(const std::string& hash_key, Instance** instance) {
  const ServiceKey& service_key = context_->GetContextImpl()->GetMetricService().service_;
  if (service_key.name_.empty() || service_key.namespace_.empty()) {
    POLARIS_LOG(LOG_ERROR, "metric service config is [%s/%s]", service_key.namespace_.c_str(),
                service_key.name_.c_str());
    return kReturnInvalidConfig;
  }
  // 通过一致性hash获取一个实例
  Criteria criteria;
  criteria.hash_string_ = hash_key;
  return ConsumerApiImpl::GetSystemServer(context_, service_key, criteria, *instance, 0);
}

ReturnCode MetricConnector::SelectConnection(const v1::MetricKey& metric_key,
                                             MetricConnection*& connection) {
  Instance* instance   = NULL;  // 通过一致性hash获取一个实例
  std::string hash_key = metric_key.namespace_() + ":" + metric_key.service();
  ReturnCode ret_code  = SelectInstance(hash_key, &instance);
  if (ret_code != kReturnOk) {
    return ret_code;
  }
  POLARIS_LOG(LOG_DEBUG, "select instance[%s:%d] for key:%s", instance->GetHost().c_str(),
              instance->GetPort(), hash_key.c_str());

  std::map<std::string, MetricConnection*>::iterator it = connection_mgr_.find(instance->GetId());
  if (it != connection_mgr_.end()) {  // 连接已存在，直接返回
    connection = it->second;
    delete instance;
    return kReturnOk;
  }
  connection                         = new MetricConnection(this, instance);
  connection_mgr_[instance->GetId()] = connection;
  return kReturnOk;
}

void MetricConnector::UpdateCallResult(Instance* instance, PolarisServerCode server_code) {
  POLARIS_ASSERT(instance != NULL);
  const ServiceKey& service = context_->GetContextImpl()->GetMetricService().service_;
  CallRetStatus status      = kCallRetOk;
  if (kServerCodeConnectError <= server_code && server_code <= kServerCodeInvalidResponse) {
    status = kCallRetError;
  }
  ConsumerApiImpl::UpdateServerResult(context_, service, *instance, server_code, status, 100);
}

}  // namespace polaris
