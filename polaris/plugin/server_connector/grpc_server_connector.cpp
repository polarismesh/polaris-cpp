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

#include "plugin/server_connector/grpc_server_connector.h"

#include <features.h>
#include <google/protobuf/message.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/wrappers.pb.h>
#include <inttypes.h>
#include <stdlib.h>
#include <v1/client.pb.h>
#include <v1/model.pb.h>
#include <v1/request.pb.h>
#include <v1/response.pb.h>
#include <v1/service.pb.h>

#include <sstream>
#include <string>
#include <utility>
#include "api/consumer_api.h"
#include "context/context_impl.h"
#include "context/service_context.h"
#include "logger.h"
#include "model/model_impl.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "polaris/log.h"
#include "polaris/polaris.h"
#include "provider/request.h"
#include "sync/future.h"
#include "utils/time_clock.h"
#include "utils/netclient.h"

namespace polaris {

ReturnCode BadRequestToReturnCode(v1::RetCode ret_code) {
  if (ret_code >= v1::NotFoundResource && ret_code <= v1::NotFoundSourceService) {
    if (ret_code == v1::NotFoundInstance) {
      return kReturnInstanceNotFound;
    }
    return kReturnServiceNotFound;
  }
  switch (ret_code) {
    // 健康检查被关闭
    case v1::HealthCheckNotOpen:
    case v1::HeartbeatOnDisabledIns:
      return kReturnHealthyCheckDisable;
    // 请求被限频
    case v1::HeartbeatExceedLimit:
      return kRetrunRateLimit;
    // 资源已存在
    case v1::ExistedResource:
      return kReturnExistedResource;
    default:
      return kReturnInvalidArgument;
  }
}

/// @brief 将服务端返回码转换成客户端返回码
ReturnCode ToClientReturnCode(const google::protobuf::UInt32Value& code) {
  int http_code = code.value() / 1000;
  switch (http_code) {
    case 200:
      return kReturnOk;
    case 500:  // 服务器执行请求失败
      return kReturnServerError;
    case 400:  // 请求不合法
      return BadRequestToReturnCode(static_cast<v1::RetCode>(code.value()));
    case 401:  // 请求未授权
      return kReturnUnauthorized;
    case 404:  // 资源未找到
      return kReturnResourceNotFound;
    default:
      return kReturnServerUnknownError;
  }
}

///////////////////////////////////////////////////////////////////////////////
DiscoverEventTask::DiscoverEventTask(GrpcServerConnector* connector, const ServiceKey& service_key,
                                     ServiceDataType data_type, uint64_t sync_interval,
                                     const std::string& disk_revision, ServiceEventHandler* handler) {
  connector_ = connector;
  service_.service_key_ = service_key;
  service_.data_type_ = data_type;
  sync_interval_ = sync_interval;
  revision_ = disk_revision;
  handler_ = handler;
}

DiscoverEventTask::~DiscoverEventTask() {
  if (handler_ != nullptr) {
    delete handler_;
  }
}

void DiscoverEventTask::Run() {
  connector_->ProcessQueuedListener(this);
  // 重置handler防止析构时被删除
  handler_ = nullptr;
}

///////////////////////////////////////////////////////////////////////////////
GrpcServerConnector::GrpcServerConnector()
    : discover_stream_state_(kDiscoverStreamNotInit),
      context_(nullptr),
      task_thread_id_(0),
      discover_instance_(nullptr),
      grpc_client_(nullptr),
      discover_stream_(nullptr),
      stream_response_time_(0),
      server_switch_interval_(0),
      server_switch_state_(kServerSwitchInit),
      message_used_time_(0),
      request_queue_size_(0),
      last_cache_version_(0) {}

GrpcServerConnector::~GrpcServerConnector() {
  // 关闭线程
  reactor_.Stop();
  if (task_thread_id_ != 0) {
    pthread_join(task_thread_id_, nullptr);
    task_thread_id_ = 0;
  }
  for (std::map<ServiceKeyWithType, ServiceListener>::iterator it = listener_map_.begin(); it != listener_map_.end();
       ++it) {
    delete it->second.handler_;
  }
  if (grpc_client_ != nullptr) {
    discover_stream_ = nullptr;
    delete grpc_client_;
  }
  if (discover_instance_ != nullptr) {
    delete discover_instance_;
    discover_instance_ = nullptr;
  }
  for (std::map<uint64_t, AsyncRequest*>::iterator it = async_request_map_.begin(); it != async_request_map_.end();
       ++it) {
    delete it->second;
  }
  context_ = nullptr;
}

ReturnCode GrpcServerConnector::InitTimeoutStrategy(Config* config) {
  static const char kConnectTimeoutKey[] = "connectTimeout";
  static const uint64_t kConnectTimeoutDefault = 200;
  static const char kConnectTimeoutMaxKey[] = "connectTimeoutMax";
  static const uint64_t kConnectTimeoutMaxDefault = 1000;
  static const char kConnectTimeoutExpandKey[] = "connectTimeoutExpand";
  static const float kConnectTimeoutExpandDefaut = 1.5;
  uint64_t timeout = config->GetMsOrDefault(kConnectTimeoutKey, kConnectTimeoutDefault);
  POLARIS_CHECK(timeout > 0, kReturnInvalidConfig);
  uint64_t max_timeout = config->GetMsOrDefault(kConnectTimeoutMaxKey, kConnectTimeoutMaxDefault);
  float expand = config->GetFloatOrDefault(kConnectTimeoutExpandKey, kConnectTimeoutExpandDefaut);
  POLARIS_CHECK(expand > 1.0, kReturnInvalidConfig);
  connect_timeout_.Init(timeout, max_timeout, expand);

  static const char kMessageTimeoutKey[] = "messageTimeout";
  static const uint64_t kMessageTimeoutDefault = 1000;
  static const char kMessageTimeoutMaxKey[] = "messageTimeoutMax";
  static const uint64_t kMessageTimeoutMaxDefault = 10 * 1000;
  static const char kMessageTimeoutExpandKey[] = "messageTimeoutExpand";
  static const float kMessageTimeoutExpandDefaut = 2.0;
  timeout = config->GetMsOrDefault(kMessageTimeoutKey, kMessageTimeoutDefault);
  POLARIS_CHECK(timeout > 0, kReturnInvalidConfig);
  max_timeout = config->GetMsOrDefault(kMessageTimeoutMaxKey, kMessageTimeoutMaxDefault);
  expand = config->GetFloatOrDefault(kMessageTimeoutExpandKey, kMessageTimeoutExpandDefaut);
  POLARIS_CHECK(expand > 1.0, kReturnInvalidConfig);
  message_timeout_.Init(timeout, max_timeout, expand);
  return kReturnOk;
}

ReturnCode GrpcServerConnector::Init(Config* config, Context* context) {
  static const char kServerAddressesKey[] = "addresses";
  //static const char kJoinPointKey[] = "joinPoint";
  //static const char kServerDefault[] = "default";

  static const char kServerSwitchIntervalKey[] = "serverSwitchInterval";
  static const int kServerSwitchIntervalDefault = 10 * 60 * 1000;

  static const char kMaxRequestQueueSizeKey[] = "requestQueueSize";
  static const int kMaxRequestQueueSizeDefault = 1000;

  context_ = context;

  // 获取埋点地址配置
  std::vector<std::string> config_server = config->GetListOrDefault(kServerAddressesKey, "");
  if (config_server.empty()) {
    POLARIS_LOG(LOG_ERROR, "get polaris server address failed");
    return kReturnInvalidConfig;
  } else if (SeedServerConfig::ParseSeedServer(config_server, server_lists_) == 0) {
    POLARIS_LOG(LOG_ERROR, "parse polaris server address failed");
    return kReturnInvalidConfig;
  }

  ContextImpl* contextImpl = context->GetContextImpl();
  // 获取IP地址
  if (contextImpl->GetSdkToken().ip().length() == 0) {
    std::string bind_ip;
    if (!NetClient::GetIpByConnect(&bind_ip, server_lists_)) {
      POLARIS_LOG(LOG_ERROR, "get client ip from polaris connection failed");
    } else {
      contextImpl->SetBindIP(bind_ip);
      POLARIS_LOG(LOG_INFO, "get local ip address by connection, sdk token ip:%s", contextImpl->GetApiBindIp().c_str());
    }
  }

  server_switch_interval_ = config->GetMsOrDefault(kServerSwitchIntervalKey,
                                                   kServerSwitchIntervalDefault);  // default 10m
  POLARIS_CHECK(server_switch_interval_ >= 60 * 1000, kReturnInvalidConfig);

  if (InitTimeoutStrategy(config) != kReturnOk) {
    return kReturnInvalidConfig;
  }

  int request_queue_size = config->GetIntOrDefault(kMaxRequestQueueSizeKey, kMaxRequestQueueSizeDefault);
  POLARIS_CHECK(request_queue_size > 0, kReturnInvalidConfig);
  request_queue_size_ = static_cast<std::size_t>(request_queue_size);

  POLARIS_LOG(LOG_INFO, "seed server list:%s", SeedServerConfig::SeedServersToString(server_lists_).c_str());

  // 创建任务执行线程
  if (task_thread_id_ == 0) {
    if (pthread_create(&task_thread_id_, nullptr, ThreadFunction, this) != 0) {
      POLARIS_LOG(LOG_ERROR, "create server connector task thread error");
      return kReturnInvalidState;
    }
#if defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 12) && !defined(COMPILE_FOR_PRE_CPP11)
    pthread_setname_np(task_thread_id_, "stream_task");
#endif
    POLARIS_LOG(LOG_INFO, "create server connector task thread success");
  }
  return kReturnOk;
}

void* GrpcServerConnector::ThreadFunction(void* arg) {
  GrpcServerConnector* server_connector = static_cast<GrpcServerConnector*>(arg);
  // 运行event loop之前，先通过切换服务器建立一个连接
  server_connector->ServerSwitch();
  server_connector->reactor_.Run();
  POLARIS_LOG(LOG_INFO, "server connector event loop exit");
  return nullptr;
}

ReturnCode GrpcServerConnector::RegisterEventHandler(const ServiceKey& service_key, ServiceDataType data_type,
                                                     uint64_t sync_interval, const std::string& disk_revision,
                                                     ServiceEventHandler* handler) {
  if (handler == nullptr) {
    POLARIS_LOG(LOG_ERROR, "register event handler for service:[%s/%s] failed since handler is null",
                service_key.namespace_.c_str(), service_key.name_.c_str());
    return kReturnInvalidArgument;
  }
  reactor_.SubmitTask(new DiscoverEventTask(this, service_key, data_type, sync_interval, disk_revision, handler));
  reactor_.Notify();
  POLARIS_LOG(LOG_INFO, "register %s event handler for service[%s/%s]", DataTypeToStr(data_type),
              service_key.namespace_.c_str(), service_key.name_.c_str());
  return kReturnOk;
}

ReturnCode GrpcServerConnector::DeregisterEventHandler(const ServiceKey& service_key, ServiceDataType data_type) {
  reactor_.SubmitTask(new DiscoverEventTask(this, service_key, data_type, 0, "", nullptr));
  reactor_.Notify();
  POLARIS_LOG(LOG_INFO, "deregister %s event handler for service[%s/%s]", DataTypeToStr(data_type),
              service_key.namespace_.c_str(), service_key.name_.c_str());
  return kReturnOk;
}

void GrpcServerConnector::ProcessQueuedListener(DiscoverEventTask* discover_event) {
  POLARIS_ASSERT(discover_event->connector_ == this);
  std::map<ServiceKeyWithType, ServiceListener>::iterator service_it = listener_map_.find(discover_event->service_);
  if (discover_event->handler_ == nullptr) {  // 服务缓存过期的情况下反注册服务监听
    // 只有注册过的服务才会反注册，本地加载的后未访问则不会有监听
    POLARIS_ASSERT(service_it != listener_map_.end());
    ServiceListener& service_listener = service_it->second;
    // 取消设置相关定时任务，防止这些任务去执行时使用已经删除的listener
    if (service_listener.discover_task_iter_ != reactor_.TimingTaskEnd()) {
      reactor_.CancelTimingTask(service_listener.discover_task_iter_);
    }
    // 如果已经发送了服务发现请求，则取消设置的超时检查任务
    if (service_listener.timeout_task_iter_ != reactor_.TimingTaskEnd()) {
      reactor_.CancelTimingTask(service_listener.timeout_task_iter_);
    }
    pending_for_connected_.erase(&service_listener);  // 有可能在等待连接，尝试取消
    // 释放缓存数据
    service_listener.handler_->OnEventUpdate(service_listener.service_.service_key_,
                                             service_listener.service_.data_type_, nullptr);
    delete service_listener.handler_;
    // 监听map中删除，这样如果服务应答了相关数据找不到监听直接丢弃即可
    listener_map_.erase(service_it);
  } else {
    POLARIS_ASSERT(service_it == listener_map_.end());  // 不能重复注册监听
    ServiceListener& service_listener = listener_map_[discover_event->service_];
    service_listener.service_ = discover_event->service_;
    service_listener.sync_interval_ = discover_event->sync_interval_;
    service_listener.revision_ = discover_event->revision_;
    service_listener.handler_ = discover_event->handler_;
    service_listener.cache_version_ = 0;
    service_listener.ret_code_ = 0;
    service_listener.discover_task_iter_ = reactor_.TimingTaskEnd();
    service_listener.timeout_task_iter_ = reactor_.TimingTaskEnd();
    service_listener.connector_ = this;
    // 立即执行服务发现任务
    if (!this->SendDiscoverRequest(service_listener)) {
      pending_for_connected_.insert(&service_listener);
    }
  }
}

void GrpcServerConnector::TimingDiscover(ServiceListener* service_listener) {
  service_listener->discover_task_iter_ = service_listener->connector_->reactor_.TimingTaskEnd();
  if (!service_listener->connector_->SendDiscoverRequest(*service_listener)) {
    service_listener->connector_->pending_for_connected_.insert(service_listener);
  }
}

bool GrpcServerConnector::SendDiscoverRequest(ServiceListener& service_listener) {
  const ServiceKey& service_key = service_listener.service_.service_key_;
  POLARIS_ASSERT(grpc_client_ != nullptr);
  if (discover_stream_ == nullptr) {  // 检查stream，不正常返回并放入等待队列
    POLARIS_LOG(LOG_TRACE, "server connector pending discover %s request for service[%s/%s]",
                DataTypeToStr(service_listener.service_.data_type_), service_key.namespace_.c_str(),
                service_key.name_.c_str());
    return false;
  }
  if (discover_stream_state_ != kDiscoverStreamInit) {
    const ServiceKey& discover_service = context_->GetContextImpl()->GetDiscoverService().service_;
    if (discover_service.name_.empty()) {
      POLARIS_LOG(LOG_INFO, "discover service is empty, state transive to DiscoverStreamInit");
      discover_stream_state_ = kDiscoverStreamInit;
    } else {
      if (service_key.name_ != discover_service.name_ || service_key.namespace_ != discover_service.namespace_) {
        POLARIS_LOG(LOG_INFO, "wait discover service before discover %s for service[%s/%s]",
                    DataTypeToStr(service_listener.service_.data_type_), service_key.namespace_.c_str(),
                    service_key.name_.c_str());
        return false;
      }
    }
  }
  // 已经发送过请求正等待超时
  if (service_listener.timeout_task_iter_ != reactor_.TimingTaskEnd()) {
    POLARIS_LOG(LOG_WARN, "already discover %s for service[%s/%s]", DataTypeToStr(service_listener.service_.data_type_),
                service_key.namespace_.c_str(), service_key.name_.c_str());
    return true;
  }
  // 组装请求
  ::v1::DiscoverRequest request;
  if (!service_key.namespace_.empty()) {
    request.mutable_service()->mutable_namespace_()->set_value(service_key.namespace_);
  }
  request.mutable_service()->mutable_name()->set_value(service_key.name_);
  request.mutable_service()->mutable_revision()->set_value(service_listener.revision_);
  if (service_listener.service_.data_type_ == kServiceDataInstances) {
    request.set_type(v1::DiscoverRequest::INSTANCE);
  } else if (service_listener.service_.data_type_ == kServiceDataRouteRule) {
    request.set_type(v1::DiscoverRequest::ROUTING);
  } else if (service_listener.service_.data_type_ == kServiceDataRateLimit) {
    request.set_type(v1::DiscoverRequest::RATE_LIMIT);
  } else if (service_listener.service_.data_type_ == kCircuitBreakerConfig) {
    request.set_type(v1::DiscoverRequest::CIRCUIT_BREAKER);
  } else {
    POLARIS_ASSERT(false);
  }
  discover_stream_->SendMessage(request, false);
  POLARIS_LOG(LOG_TRACE, "server connector try send discover %s request for service[%s/%s]",
              DataTypeToStr(service_listener.service_.data_type_), service_key.namespace_.c_str(),
              service_key.name_.c_str());
  // 设置超时检查任务
  service_listener.timeout_task_iter_ = reactor_.AddTimingTask(
      new TimingFuncTask<ServiceListener>(DiscoverTimoutCheck, &service_listener, message_timeout_.GetTimeout()));
  return true;
}

void GrpcServerConnector::DiscoverTimoutCheck(ServiceListener* service_listener) {
  // 超时的情况下一定是未反注册的，那么这里需要触发切换
  // 这里一个流上只要有第一个请求超时了，那么触发切换就会取消其他已发送未应答的服务的超时检查任务
  service_listener->timeout_task_iter_ = service_listener->connector_->reactor_.TimingTaskEnd();
  const ServiceKey& service_key = service_listener->service_.service_key_;
  POLARIS_LOG(LOG_INFO, "server switch because discover [%s/%s] timeout[%" PRIu64 "]", service_key.namespace_.c_str(),
              service_key.name_.c_str(), service_listener->connector_->message_timeout_.GetTimeout());
  service_listener->connector_->message_timeout_.SetNextRetryTimeout();
  // 加入pending等待连接成功后发送
  service_listener->connector_->pending_for_connected_.insert(service_listener);
  service_listener->connector_->UpdateCallResult(kServerCodeRpcTimeout,
                                                 service_listener->connector_->message_timeout_.GetTimeout());
  service_listener->connector_->ServerSwitch();
}

bool GrpcServerConnector::UpdateRevision(ServiceListener& listener, const ::v1::DiscoverResponse& response) {
  if (response.code().value() == v1::DataNoChange) {
    // 数据没有变化，首次返回时记录下返回码
    if (listener.ret_code_ != response.code().value()) {
      listener.ret_code_ = response.code().value();
    }
    return false;
  }
  if (response.code().value() == v1::ExecuteSuccess) {
    const std::string& new_revision = response.service().revision().value();
    if (listener.ret_code_ == response.code().value() && new_revision == listener.revision_) {
      return false;
    }
    listener.ret_code_ = response.code().value();
    listener.revision_ = new_revision;
    listener.cache_version_ = ++last_cache_version_;
    return true;
  }

  if (listener.ret_code_ == response.code().value()) {
    return false;  // 不是第一次返回服务不存在
  }
  listener.ret_code_ = response.code().value();
  // 由于Server在非正常时，会返回客户端发送的revision，首次返回非正常返回时，重置revision
  listener.revision_.clear();
  listener.cache_version_ = ++last_cache_version_;
  return true;
}

void GrpcServerConnector::OnReceiveMessage(v1::DiscoverResponse* response) {
  stream_response_time_ = Time::GetCoarseSteadyTimeMs();
  PolarisServerCode server_code = ToPolarisServerCode(response->code().value());
  if (server_code == kServerCodeReturnOk ||
      (server_code == kServerCodeInvalidRequest && ToClientReturnCode(response->code()) == kReturnServiceNotFound)) {
    ProcessDiscoverResponse(*response);
  } else {
    POLARIS_LOG(LOG_ERROR, "discover stream response with server error:%d-%s", response->code().value(),
                response->info().value().c_str());
    this->UpdateCallResult(server_code, message_timeout_.GetTimeout());
    if (server_code == kServerCodeServerError) {  // 服务错误，切换Server重试
      // 触发server切换，所有超时检查会取消，并重新等待连接连接后重新发送
      this->ServerSwitch();
    }
  }
  delete response;
}

ReturnCode GrpcServerConnector::ProcessDiscoverResponse(::v1::DiscoverResponse& response) {
  const ::v1::Service& resp_service = response.service();
  ServiceKeyWithType service_with_type;
  service_with_type.service_key_.namespace_ = resp_service.namespace_().value();
  service_with_type.service_key_.name_ = resp_service.name().value();
  const ServiceKey& service_key = service_with_type.service_key_;
  // 检查服务发现返回类型是否正确
  if (response.type() == v1::DiscoverResponse::INSTANCE) {
    service_with_type.data_type_ = kServiceDataInstances;
  } else if (response.type() == v1::DiscoverResponse::ROUTING) {
    service_with_type.data_type_ = kServiceDataRouteRule;
  } else if (response.type() == v1::DiscoverResponse::RATE_LIMIT) {
    service_with_type.data_type_ = kServiceDataRateLimit;
  } else if (response.type() == v1::DiscoverResponse::CIRCUIT_BREAKER) {
    service_with_type.data_type_ = kCircuitBreakerConfig;
  } else {
    POLARIS_LOG(LOG_ERROR, "receive discover response for service[%s/%s] with unknown type: %d",
                service_key.namespace_.c_str(), service_key.name_.c_str(), static_cast<int>(response.type()));
    this->UpdateCallResult(kServerCodeInvalidResponse, 0);
    return kReturnOk;
  }
  // 查找服务监听，处理应答
  std::map<ServiceKeyWithType, ServiceListener>::iterator service_it = listener_map_.find(service_with_type);
  if (service_it == listener_map_.end()) {
    POLARIS_LOG(LOG_INFO, "discover %s for service[%s/%s], but handler was deregister",
                DataTypeToStr(service_with_type.data_type_), service_key.namespace_.c_str(), service_key.name_.c_str());
    return kReturnOk;
  }

  uint64_t delay = 0;
  ServiceListener& listener = service_it->second;
  // 处理超时检查请求
  if (listener.timeout_task_iter_ != reactor_.TimingTaskEnd()) {
    delay = Time::GetCoarseSteadyTimeMs() + message_timeout_.GetTimeout();
    delay = delay > listener.timeout_task_iter_->first ? delay - listener.timeout_task_iter_->first : 0;
    reactor_.CancelTimingTask(listener.timeout_task_iter_);
  }

  ReturnCode ret = ToClientReturnCode(response.code());
  if (ret == kReturnOk || ret == kReturnServiceNotFound) {  // 返回NotFound的时候也需要触发Handler
    this->UpdateCallResult(kServerCodeReturnOk, delay);
    this->UpdateMaxUsedTime(delay);  // 更新最大discover请求耗时
    if (UpdateRevision(listener, response)) {
      ServiceData* event_data =
          ServiceData::CreateFromPb(reinterpret_cast<void*>(&response),
                                    ret == kReturnOk ? kDataIsSyncing : kDataNotFound, listener.cache_version_);
      // 执行回调
      listener.handler_->OnEventUpdate(service_key, event_data->GetDataType(), event_data);
      POLARIS_LOG(LOG_INFO, "update service %s for service[%s/%s]", DataTypeToStr(service_with_type.data_type_),
                  service_key.namespace_.c_str(), service_key.name_.c_str());
    } else {
      listener.handler_->OnEventSync(service_key, listener.service_.data_type_);
      if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
        POLARIS_LOG(LOG_TRACE, "skip update %s for service[%s/%s] because of same revision[%s] and code: %d",
                    DataTypeToStr(service_with_type.data_type_), service_key.namespace_.c_str(),
                    service_key.name_.c_str(), resp_service.revision().value().c_str(), response.code().value());
      }
    }
    if (ret == kReturnOk && discover_stream_state_ < kDiscoverStreamGetInstance) {
      const ServiceKey discover_service = context_->GetContextImpl()->GetDiscoverService().service_;
      if (service_with_type.data_type_ == kServiceDataInstances && service_key == discover_service) {
        server_switch_state_ = kServerSwitchDefault;
        discover_stream_state_ = kDiscoverStreamGetInstance;
        reactor_.CancelTimingTask(server_switch_task_iter_);  // 取消定时切换任务，触发立即切换
        server_switch_task_iter_ = reactor_.AddTimingTask(
            new TimingFuncTask<GrpcServerConnector>(GrpcServerConnector::TimingServerSwitch, this, 0));
        POLARIS_LOG(LOG_INFO, "discover stream will switch from seed server to service");
      }
    }
  } else {
    this->UpdateCallResult(kServerCodeInvalidRequest, delay);
    POLARIS_LOG(LOG_ERROR, "discover %s for service[%s/%s] with server error[%" PRId32 ":%s]",
                DataTypeToStr(service_with_type.data_type_), service_key.namespace_.c_str(), service_key.name_.c_str(),
                response.code().value(), response.info().value().c_str());
  }
  // 设置下一次的检查任务
  // 这里检查，避免发现任务取消又注册后，之前的同步任务遗留的应答导致重复设置定时任务
  if (listener.discover_task_iter_ == reactor_.TimingTaskEnd()) {
    listener.discover_task_iter_ =
        reactor_.AddTimingTask(new TimingFuncTask<ServiceListener>(TimingDiscover, &listener, listener.sync_interval_));
    if (pending_for_connected_.count(&listener) > 0) {
      // 清理由于切换失败保留在pending列表里的任务需要
      pending_for_connected_.erase(&listener);
    }
  }
  return kReturnOk;
}

void GrpcServerConnector::OnRemoteClose(const std::string& message) {
  POLARIS_LOG(LOG_ERROR, "discover stream close by remote with error: %s", message.c_str());
  // 触发server切换，所有超时检查会取消，并重新等待连接连接后重新发送
  this->UpdateCallResult(kServerCodeRemoteClose, connect_timeout_.GetTimeout());
  this->ServerSwitch();
}

void GrpcServerConnector::TimingServerSwitch(GrpcServerConnector* server_connector) {
  POLARIS_ASSERT(server_connector != nullptr);
  if (server_connector->server_switch_state_ == kServerSwitchNormal) {  // 正常状态下
    server_connector->server_switch_state_ = kServerSwitchPeriodic;
    POLARIS_LOG(LOG_INFO, "switch from server[%s] with timing[%" PRIu64 "]",
                server_connector->grpc_client_->CurrentServer().c_str(), server_connector->server_switch_interval_);
    // 根据上一个连接使用的消息耗时重置消息超时时间
    if (server_connector->message_used_time_ > 0) {
      server_connector->message_timeout_.SetNormalTimeout(server_connector->message_used_time_);
      server_connector->message_used_time_ = 0;
    }
    server_connector->UpdateCallResult(kServerCodeReturnOk, 0);
  } else if (server_connector->server_switch_state_ == kServerSwitchDefault) {
    server_connector->server_switch_state_ = kServerSwitchPeriodic;
    POLARIS_LOG(LOG_INFO, "switch from seed server[%s] to seed service",
                server_connector->grpc_client_->CurrentServer().c_str());
    server_connector->UpdateCallResult(kServerCodeReturnOk, 0);
  } else if (server_connector->server_switch_state_ == kServerSwitchBegin) {
    server_connector->server_switch_state_ = kServerSwitchTimeout;
    POLARIS_LOG(LOG_INFO, "switch from server[%s] with connect timeout[%" PRIu64 "]",
                server_connector->grpc_client_->CurrentServer().c_str(),
                server_connector->connect_timeout_.GetTimeout());
    server_connector->connect_timeout_.SetNextRetryTimeout();  // 扩张连接超时时间
    server_connector->UpdateCallResult(kServerCodeConnectError, server_connector->connect_timeout_.GetTimeout());
  }
  server_connector->ServerSwitch();
}

ReturnCode GrpcServerConnector::SelectInstance(const ServiceKey& service_key, uint32_t timeout, Instance** instance,
                                               bool ignore_half_open) {
  Criteria criteria;
  criteria.ignore_half_open_ = ignore_half_open;
  return ConsumerApiImpl::GetSystemServer(context_, service_key, criteria, *instance, timeout);
}

SeedServer& GrpcServerConnector::SelectSeed() {
  return server_lists_[rand() % server_lists_.size()];
}

void GrpcServerConnector::ServerSwitch() {
  if (server_switch_state_ == kServerSwitchNormal       // 服务调用出错或超时触发切换
      || server_switch_state_ == kServerSwitchBegin) {  // 切换后异步连接回调触发重新
    reactor_.CancelTimingTask(server_switch_task_iter_);
  }

  // 有超时检查的服务，说明本次未完成服务发现，加入pending列表，从而可在切换成功后立马发送
  for (std::map<ServiceKeyWithType, ServiceListener>::iterator it = listener_map_.begin(); it != listener_map_.end();
       ++it) {
    if (it->second.timeout_task_iter_ != reactor_.TimingTaskEnd()) {
      reactor_.CancelTimingTask(it->second.timeout_task_iter_);
      pending_for_connected_.insert(&it->second);
    }
  }

  // 选择一个服务器
  std::string host;
  int port = 0;
  const ServiceKey& discover_service = context_->GetContextImpl()->GetDiscoverService().service_;
  if (!discover_service.name_.empty() && discover_stream_state_ >= kDiscoverStreamGetInstance) {  // 说明内部服务已经返回
    if (discover_instance_ != nullptr) {
      delete discover_instance_;
      discover_instance_ = nullptr;
    }
    bool ignore_half_open = server_switch_state_ != kServerSwitchPeriodic;  // 周期切换才选半开节点
    ReturnCode ret_code = SelectInstance(discover_service, 0, &discover_instance_, ignore_half_open);
    if (ret_code == kReturnOk) {
      POLARIS_ASSERT(discover_instance_ != nullptr);
      host = discover_instance_->GetHost();
      port = discover_instance_->GetPort();
      discover_stream_state_ = kDiscoverStreamInit;
      POLARIS_LOG(LOG_INFO, "discover stream switch to discover server[%s:%d]", host.c_str(), port);
    } else {
      POLARIS_LOG(LOG_WARN, "discover polaris service[%s/%s] return [%s], switch to seed server",
                  discover_service.namespace_.c_str(), discover_service.name_.c_str(),
                  ReturnCodeToMsg(ret_code).c_str());
    }
  }
  if (host.empty()) {
    discover_stream_state_ = kDiscoverStreamNotInit;
    SeedServer& server = SelectSeed();
    host = server.ip_;
    port = server.port_;
    POLARIS_LOG(LOG_INFO, "discover stream switch to seed server[%s:%d]", host.c_str(), port);
  }

  // 设置定时任务进行超时检查
  server_switch_state_ = kServerSwitchBegin;
  server_switch_task_iter_ = reactor_.AddTimingTask(
      new TimingFuncTask<GrpcServerConnector>(TimingServerSwitch, this, connect_timeout_.GetTimeout()));
  if (grpc_client_ != nullptr) {  // 删除旧连接客户端
    discover_stream_ = nullptr;   // 重置stream为NULL
    delete grpc_client_;
  }
  grpc_client_ = new grpc::GrpcClient(reactor_);
  grpc_client_->Connect(
      host, port, connect_timeout_.GetTimeout(),
      std::bind(&GrpcServerConnector::OnDiscoverConnect, this, Time::GetCoarseSteadyTimeMs(), std::placeholders::_1));
}

void GrpcServerConnector::OnDiscoverConnect(uint64_t begin_time, ReturnCode return_code) {
  POLARIS_ASSERT(grpc_client_ != nullptr);
  POLARIS_ASSERT(discover_stream_ == nullptr);
  if (return_code != kReturnOk) {
    POLARIS_LOG(LOG_INFO, "connect to server[%s] return %d", grpc_client_->CurrentServer().c_str(), return_code);
    // 这里不直接触发重新切换Server，让链接超时检查去切换
    return;
  }

  // 用当前连接建立耗时设置下一次连接超时时间
  uint64_t connect_used_time = Time::GetCoarseSteadyTimeMs() - begin_time;
  connect_timeout_.SetNormalTimeout(connect_used_time);

  POLARIS_ASSERT(server_switch_state_ == kServerSwitchBegin);
  server_switch_state_ = kServerSwitchNormal;
  reactor_.CancelTimingTask(server_switch_task_iter_);  // 取消超时检查
  // 设置正常的周期切换
  server_switch_task_iter_ = reactor_.AddTimingTask(
      new TimingFuncTask<GrpcServerConnector>(GrpcServerConnector::TimingServerSwitch, this, server_switch_interval_));
  POLARIS_LOG(LOG_INFO, "connect to server[%s] used[%" PRIu64 "], send %zu pending discover request",
              grpc_client_->CurrentServer().c_str(), connect_used_time, pending_for_connected_.size());
  // 创建stream 发起pending的请求
  discover_stream_ = grpc_client_->StartStream("/v1.PolarisGRPC/Discover", *this);
  stream_response_time_ = Time::GetCoarseSteadyTimeMs();
  std::set<ServiceListener*> need_pending;
  for (auto& pending_request : pending_for_connected_) {
    if (!SendDiscoverRequest(*pending_request)) {
      need_pending.insert(pending_request);
    }
  }
  pending_for_connected_.swap(need_pending);
}

void GrpcServerConnector::UpdateCallResult(PolarisServerCode server_code, uint64_t delay) {
  if (discover_instance_ == nullptr) {
    return;  // 内部埋点实例 不要上报
  }
  const ServiceKey& service = context_->GetContextImpl()->GetDiscoverService().service_;
  CallRetStatus status = kCallRetOk;
  if (kServerCodeConnectError <= server_code && server_code <= kServerCodeInvalidResponse) {
    if (server_code == kServerCodeRpcTimeout && stream_response_time_ + delay > Time::GetCoarseSteadyTimeMs()) {
      status = kCallRetOk;
    } else {  // 消息超时且stream上超时时间内没有数据返回则上报失败
      status = kCallRetError;
    }
  }
  ConsumerApiImpl::UpdateServerResult(context_, service, *discover_instance_, server_code, status, delay);
}

BlockRequest* GrpcServerConnector::CreateBlockRequest(PolarisRequestType request_type, uint64_t timeout) {
  return new BlockRequest(request_type, *this, timeout);
}

ReturnCode GrpcServerConnector::RegisterInstance(const InstanceRegisterRequest& req, uint64_t timeout_ms,
                                                 std::string& instance_id) {
  if (timeout_ms == 0) {
    return kReturnInvalidArgument;
  }
  BlockRequest* block_request = CreateBlockRequest(kBlockRegisterInstance, timeout_ms);
  if (!block_request->PrepareClient()) {  // 准备连接
    delete block_request;
    return kReturnNetworkFailed;
  }

  // 设置服务信息
  v1::Instance* instance = req.GetImpl().ToPb();

  // 序列化并提交请求，获取应答future等待，request交给超时检查任务释放
  Future<v1::Response>* future = block_request->SendRequest(instance);
  if (!future->Wait(block_request->GetTimeout()) || !future->IsReady()) {
    delete future;
    return kReturnTimeout;
  }
  ReturnCode ret_code = kReturnOk;
  if (future->IsFailed()) {
    ret_code = future->GetError();
  } else {
    v1::Response* response = future->GetValue();
    ret_code = ToClientReturnCode(response->code());
    if (ret_code == kReturnOk || ret_code == kReturnExistedResource) {
      instance_id = response->instance().id().value();
    }
    delete response;
  }
  delete future;
  return ret_code;
}

ReturnCode GrpcServerConnector::DeregisterInstance(const InstanceDeregisterRequest& req, uint64_t timeout_ms) {
  if (timeout_ms == 0) {
    return kReturnInvalidArgument;
  }

  BlockRequest* block_request = CreateBlockRequest(kBlockDeregisterInstance, timeout_ms);
  if (!block_request->PrepareClient()) {
    delete block_request;
    return kReturnNetworkFailed;
  }

  v1::Instance* instance = req.GetImpl().ToPb();

  Future<v1::Response>* future = block_request->SendRequest(instance);
  if (!future->Wait(block_request->GetTimeout()) || !future->IsReady()) {
    delete future;
    return kReturnTimeout;
  }
  ReturnCode ret_code = kReturnOk;
  if (future->IsFailed()) {
    ret_code = future->GetError();
  } else {
    v1::Response* response = future->GetValue();
    ret_code = ToClientReturnCode(response->code());
    delete response;
  }
  delete future;
  return ret_code;
}

ReturnCode GrpcServerConnector::InstanceHeartbeat(const InstanceHeartbeatRequest& req, uint64_t timeout_ms) {
  if (timeout_ms == 0) {
    return kReturnInvalidArgument;
  }
  BlockRequest* block_request = CreateBlockRequest(kPolarisHeartbeat, timeout_ms);
  if (!block_request->PrepareClient()) {
    delete block_request;
    return kReturnNetworkFailed;
  }

  v1::Instance* instance = req.GetImpl().ToPb();

  Future<v1::Response>* future = block_request->SendRequest(instance);
  if (!future->Wait(block_request->GetTimeout()) || !future->IsReady()) {
    delete future;
    return kReturnTimeout;
  }
  ReturnCode ret_code = kReturnOk;
  if (future->IsFailed()) {
    ret_code = future->GetError();
  } else {
    v1::Response* response = future->GetValue();
    ret_code = ToClientReturnCode(response->code());
    delete response;
  }
  delete future;
  return ret_code;
}

ReturnCode GrpcServerConnector::AsyncInstanceHeartbeat(const InstanceHeartbeatRequest& req, uint64_t timeout_ms,
                                                       ProviderCallback* callback) {
  if (timeout_ms == 0) {
    return kReturnInvalidArgument;
  }
  v1::Instance* instance = req.GetImpl().ToPb();
  uint64_t request_id = Utils::GetNextSeqId();
  std::shared_ptr<ProviderCallback> heartbeat_callback(callback);
  PolarisCallback polaris_callback = [=](ReturnCode ret_code, const std::string& message,
                                         std::unique_ptr<v1::Response>) {
    heartbeat_callback->Response(ret_code, message);
  };
  AsyncRequest* request =
      new AsyncRequest(reactor_, this, kPolarisHeartbeat, request_id, instance, timeout_ms, polaris_callback);
  reactor_.SubmitTask(new AsyncRequestSubmit(request, 20));
  return kReturnOk;
}

ReturnCode GrpcServerConnector::AsyncReportClient(const std::string& host, uint64_t timeout_ms,
                                                  PolarisCallback callback) {
  if (host.empty()) {
    return kReturnInvalidArgument;
  }
  if (timeout_ms == 0) {
    return kReturnInvalidArgument;
  }
  v1::Client* client = new v1::Client();
  client->mutable_host()->set_value(host);
  client->mutable_version()->set_value(polaris::GetVersion());
  client->set_type(v1::Client_ClientType_SDK);

  uint64_t request_id = Utils::GetNextSeqId();
  AsyncRequest* request =
      new AsyncRequest(reactor_, this, kPolarisReportClient, request_id, client, timeout_ms, callback);
  reactor_.SubmitTask(new AsyncRequestSubmit(request, 100));
  return kReturnOk;
}

const char* GrpcServerConnector::GetCallPath(PolarisRequestType request_type) {
  switch (request_type) {
    case kBlockRegisterInstance:
      return "/v1.PolarisGRPC/RegisterInstance";
    case kBlockDeregisterInstance:
      return "/v1.PolarisGRPC/DeregisterInstance";
    case kPolarisHeartbeat:
      return "/v1.PolarisGRPC/Heartbeat";
    case kPolarisReportClient:
      return "/v1.PolarisGRPC/ReportClient";
    default:
      POLARIS_ASSERT(false);
      return "";
  }
}

bool GrpcServerConnector::GetInstance(BlockRequest* block_request) {
  POLARIS_ASSERT(block_request != nullptr);
  POLARIS_ASSERT(block_request->instance_ == nullptr);
  const ServiceKey& service = GetPolarisService(context_, block_request->request_type_);
  if (service.name_.empty()) {
    SeedServer& seedServer = SelectSeed();
    block_request->host_ = seedServer.ip_;
    block_request->port_ = seedServer.port_;
    return true;
  }
  ReturnCode ret_code = SelectInstance(service, block_request->request_timeout_, &block_request->instance_);
  if (ret_code == kReturnOk) {
    POLARIS_ASSERT(block_request->instance_ != nullptr);
    POLARIS_LOG(LOG_DEBUG, "get server:%s:%d for %s", block_request->instance_->GetHost().c_str(),
                block_request->instance_->GetPort(), PolarisRequestTypeStr(block_request->request_type_));
    block_request->host_ = block_request->instance_->GetHost();
    block_request->port_ = block_request->instance_->GetPort();
    return true;
  } else {
    POLARIS_ASSERT(block_request->instance_ == nullptr);
    POLARIS_LOG(LOG_ERROR, "get server for %s with error:%s", PolarisRequestTypeStr(block_request->request_type_),
                ReturnCodeToMsg(ret_code).c_str());
  }
  return false;
}

void GrpcServerConnector::UpdateCallResult(BlockRequest* block_request) {
  if(block_request->instance_ == nullptr) {
    return;
  }
  const ServiceKey& service = GetPolarisService(context_, block_request->request_type_);
  CallRetStatus status = kCallRetOk;
  if (kServerCodeConnectError <= block_request->server_code_ &&
      block_request->server_code_ <= kServerCodeInvalidResponse) {
    status = kCallRetError;
  }
  uint64_t delay = Time::GetCoarseSteadyTimeMs() - block_request->call_begin_;
  ConsumerApiImpl::UpdateServerResult(context_, service, *block_request->instance_, block_request->server_code_, status,
                                      delay);
  delete block_request->instance_;
  block_request->instance_ = nullptr;
}

///////////////////////////////////////////////////////////////////////////////
BlockRequest::BlockRequest(PolarisRequestType request_type, GrpcServerConnector& connector, uint64_t request_timeout)
    : request_type_(request_type),
      connector_(connector),
      request_timeout_(request_timeout),
      server_code_(kServerCodeReturnOk),
      call_begin_(Time::GetCoarseSteadyTimeMs()),
      message_(nullptr),
      promise_(nullptr),
      instance_(nullptr),
      host_(""),
      port_(0),
      grpc_client_(nullptr)  {}

BlockRequest::~BlockRequest() {
  if (instance_ != nullptr) {
    delete instance_;
    instance_ = nullptr;
  }
  if (message_ != nullptr) {
    delete message_;
    message_ = nullptr;
  }
  if (promise_ != nullptr) {
    delete promise_;
    promise_ = nullptr;
  }
  if (grpc_client_ != nullptr) {
    delete grpc_client_;
    grpc_client_ = nullptr;
  }
}

void BlockRequest::OnSuccess(::v1::Response* response) {
  // RPC调用正常，且请求处理执行正确
  server_code_ = ToPolarisServerCode(response->code().value());
  if (server_code_ != kServerCodeServerError) {
    if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
      POLARIS_LOG(LOG_TRACE, "%s for request[%s] to server[%s:%d] success with response[%s]",
                  PolarisRequestTypeStr(request_type_), message_->ShortDebugString().c_str(),
                  instance_->GetHost().c_str(), instance_->GetPort(), response->ShortDebugString().c_str());
    }
    promise_->SetValue(response);
  } else {  // 请求处理失败
    promise_->SetError(kReturnServerError);
    POLARIS_LOG(LOG_ERROR, "%s for request[%s] to server[%s:%d] error with response[%s]",
                PolarisRequestTypeStr(request_type_), message_->ShortDebugString().c_str(),
                instance_->GetHost().c_str(), instance_->GetPort(), response->ShortDebugString().c_str());
    delete response;
  }
  connector_.UpdateCallResult(this);
}

void BlockRequest::OnFailure(const std::string& message) {
  POLARIS_LOG(LOG_ERROR, "%s for request[%s] to server[%s:%d] with rpc error %s", PolarisRequestTypeStr(request_type_),
              message_->ShortDebugString().c_str(), instance_->GetHost().c_str(), instance_->GetPort(),
              message.c_str());
  server_code_ = kServerCodeRpcError;
  promise_->SetError(kReturnNetworkFailed);
  connector_.UpdateCallResult(this);
}

bool BlockRequest::PrepareClient() {
  uint64_t begin_time = Time::GetCoarseSteadyTimeMs();
  if (!connector_.GetInstance(this)) {  // 选择服务实例
    return false;
  }

  // 建立grpc客户端，并尝试连接
  grpc_client_ = new grpc::GrpcClient(connector_.GetReactor());
  if (!grpc_client_->ConnectTo(host_, port_) ||
      !grpc_client_->WaitConnected(request_timeout_)) {
    POLARIS_LOG(LOG_ERROR, "%s connect to server[%s:%d] timeout", PolarisRequestTypeStr(request_type_),
                host_.c_str(), port_);
    server_code_ = kServerCodeConnectError;
    connector_.UpdateCallResult(this);
    return false;
  }
  uint64_t use_time = Time::GetCoarseSteadyTimeMs() - begin_time;
  if (use_time >= request_timeout_) {
    POLARIS_LOG(LOG_ERROR, "%s connect to server[%s:%d] timeout", PolarisRequestTypeStr(request_type_),
                host_.c_str(), port_);
    server_code_ = kServerCodeConnectError;
    connector_.UpdateCallResult(this);
    return false;
  }
  request_timeout_ -= use_time;
  return true;
}

Future<v1::Response>* BlockRequest::SendRequest(google::protobuf::Message* message) {
  POLARIS_ASSERT(message != nullptr);
  POLARIS_ASSERT(message_ == nullptr);
  POLARIS_ASSERT(promise_ == nullptr);
  message_ = message;
  promise_ = new Promise<v1::Response>();
  connector_.GetReactor().SubmitTask(new BlockRequestTask(this));
  return promise_->GetFuture();
}

///////////////////////////////////////////////////////////////////////////////
BlockRequestTask::BlockRequestTask(BlockRequest* request) : request_(request) {}

BlockRequestTask::~BlockRequestTask() {
  if (request_ != nullptr) {  // 说明任务未执行，还未提交超时检查任务，在此处释放
    delete request_;
    request_ = nullptr;
  }
}

void BlockRequestTask::Run() {
  POLARIS_ASSERT(request_->promise_ != nullptr);
  POLARIS_ASSERT(request_->grpc_client_ != nullptr);
  request_->grpc_client_->SubmitToReactor();  // 把连接建立成功的http2client加入event loop
  request_->grpc_client_->SendRequest(*request_->message_, GrpcServerConnector::GetCallPath(request_->request_type_),
                                      request_->request_timeout_, *request_);
  // 提交超时检查
  request_->connector_.GetReactor().AddTimingTask(new BlockRequestTimeout(request_, request_->request_timeout_));
  request_ = nullptr;  // request交给超时检查任务释放
}

///////////////////////////////////////////////////////////////////////////////
BlockRequestTimeout::BlockRequestTimeout(BlockRequest* request, uint64_t timeout)
    : TimingTask(timeout), request_(request) {
  request_->server_code_ = kServerCodeRpcTimeout;
}

BlockRequestTimeout::~BlockRequestTimeout() {
  POLARIS_ASSERT(request_ != nullptr);
  delete request_;  // 只能在最后超时的时候释放
  request_ = nullptr;
}

void BlockRequestTimeout::Run() {
  if (request_->instance_ != nullptr) {
    POLARIS_LOG(LOG_ERROR, "%s request[%s] to server[%s:%d] timeout", PolarisRequestTypeStr(request_->request_type_),
                request_->message_->ShortDebugString().c_str(), request_->instance_->GetHost().c_str(),
                request_->instance_->GetPort());
    request_->connector_.UpdateCallResult(request_);
  }
}

///////////////////////////////////////////////////////////////////////////////

AsyncRequest::AsyncRequest(Reactor& reactor, GrpcServerConnector* connector, PolarisRequestType request_type,
                           uint64_t request_id, google::protobuf::Message* request, uint64_t timeout,
                           PolarisCallback callback)
    : reactor_(reactor),
      connector_(connector),
      request_type_(request_type),
      request_id_(request_id),
      request_(request),
      begin_time_(Time::GetCoarseSteadyTimeMs()),
      timeout_(timeout),
      callback_(callback),
      server_(nullptr),
      host_(""),
      port_(0),
      client_(nullptr),
      timing_task_(connector->GetReactor().TimingTaskEnd()) {}

AsyncRequest::~AsyncRequest() {
  connector_ = nullptr;
  if (request_ != nullptr) {
    delete request_;
    request_ = nullptr;
  }
  if (server_ != nullptr) {
    delete server_;
    server_ = nullptr;
  }
  if (client_ != nullptr) {
    delete client_;
    client_ = nullptr;
  }
}

bool AsyncRequest::Submit() {
  if (connector_->async_request_map_.count(request_id_)) {  // ID轮回了，说明请求太多
    callback_(kRetrunRateLimit, "too many request", nullptr);
    return false;
  }

  const ServiceKey& service = GetPolarisService(connector_->context_, request_type_);
  if (service.name_.empty()) {
    SeedServer& seedServer = connector_->SelectSeed();
    host_ = seedServer.ip_;
    port_ = seedServer.port_;
  } else {
    ReturnCode ret_code = connector_->SelectInstance(service, 0, &server_);
    if (ret_code != kReturnOk) {
      callback_(ret_code, "select server failed", nullptr);
      return false;
    }
    host_ = server_->GetHost();
    port_ = server_->GetPort();
  }
  connector_->async_request_map_[request_id_] = this;  // 记录请求
  // 尝试建立连接
  client_ = new grpc::GrpcClient(reactor_);
  client_->Connect(host_, port_, GetTimeLeft(),
                   std::bind(&AsyncRequest::OnConnect, this, std::placeholders::_1));
  return true;
}

bool AsyncRequest::CheckServiceReady() {
  ContextImpl* context_impl = connector_->context_->GetContextImpl();
  const ServiceKey& service = GetPolarisService(connector_->context_, request_type_);
  context_impl->RcuEnter();
  ServiceContext* service_context = context_impl->GetServiceContext(service);
  if (service_context == nullptr) {
    context_impl->RcuExit();
    return false;
  }
  context_impl->RcuExit();

  bool is_ready = false;
  RouteInfo route_info(service, nullptr);
  ServiceRouterChain* service_route_chain = service_context->GetServiceRouterChain();
  RouteInfoNotify* notify = service_route_chain->PrepareRouteInfoWithNotify(route_info);
  if (notify == nullptr) {
    is_ready = true;
  } else {
    is_ready = notify->IsDataReady(false);
    delete notify;
  }
  return is_ready;
}

uint64_t AsyncRequest::GetTimeLeft() const {
  uint64_t deadline = begin_time_ + timeout_;
  uint64_t current_time = Time::GetCoarseSteadyTimeMs();
  return deadline > current_time ? deadline - current_time : 0;
}

void AsyncRequest::OnConnect(ReturnCode return_code) {
  if (return_code != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "connect to %s server[%s] return %d", PolarisRequestTypeStr(request_type_),
                client_->CurrentServer().c_str(), return_code);
    callback_(kReturnNetworkFailed, "connect to service failed", nullptr);
    this->Complete(return_code == kReturnTimeout ? kServerCodeRpcTimeout : kServerCodeConnectError);
    return;
  }
  // 连接成功，发送请求
  uint64_t time_left = GetTimeLeft();
  if (time_left <= 0) {  // 连接成功但请求已经超时
    callback_(kReturnNetworkFailed, "connect to server timeout", nullptr);
    this->Complete(kServerCodeRpcTimeout);
    return;
  }
  client_->SendRequest(*request_, GrpcServerConnector::GetCallPath(request_type_), time_left, *this);
  POLARIS_LOG(LOG_DEBUG, "send %s request to server[%s] success", PolarisRequestTypeStr(request_type_),
              client_->CurrentServer().c_str());
  timing_task_ = reactor_.AddTimingTask(new TimingFuncTask<AsyncRequest>(RequsetTimeoutCheck, this, time_left));
}

void AsyncRequest::RequsetTimeoutCheck(AsyncRequest* request) {
  POLARIS_LOG(LOG_ERROR, "%s request to server[%s] timeout", PolarisRequestTypeStr(request->request_type_),
              request->client_->CurrentServer().c_str());
  request->callback_(kReturnNetworkFailed, "request service timeout", nullptr);
  request->timing_task_ = request->reactor_.TimingTaskEnd();
  request->Complete(kServerCodeRpcTimeout);
}

void AsyncRequest::OnSuccess(::v1::Response* response) {
  if (timing_task_ == reactor_.TimingTaskEnd()) {
    delete response;
    return;  // 已经超时了
  }
  reactor_.CancelTimingTask(timing_task_);
  if (POLARIS_LOG_ENABLE(kDebugLogLevel)) {
    POLARIS_LOG(LOG_DEBUG, "send async %s to server[%s] response[%s]", PolarisRequestTypeStr(request_type_),
                client_->CurrentServer().c_str(), response->ShortDebugString().c_str());
  }

  ReturnCode ret_code = ToClientReturnCode(response->code());
  PolarisServerCode server_code = ToPolarisServerCode(response->code().value());
  callback_(ret_code, response->info().value(), std::unique_ptr<v1::Response>(response));
  this->Complete(server_code);
}

void AsyncRequest::OnFailure(const std::string& message) {
  if (timing_task_ == reactor_.TimingTaskEnd()) {
    return;  // 已经超时了
  }
  reactor_.CancelTimingTask(timing_task_);

  POLARIS_LOG(LOG_ERROR, "async %s request[%s] to server[%s] with rpc error %s", PolarisRequestTypeStr(request_type_),
              request_->ShortDebugString().c_str(), client_->CurrentServer().c_str(), message.c_str());
  callback_(kReturnNetworkFailed, "send request with rpc error", nullptr);
  this->Complete(kServerCodeRpcError);
}

void AsyncRequest::Complete(PolarisServerCode server_code) {
  if (server_ != nullptr) {  // 上报调用结果
    const ServiceKey& service = GetPolarisService(connector_->context_, request_type_);
    uint64_t delay = Time::GetCoarseSteadyTimeMs() - begin_time_;
    CallRetStatus status = kCallRetOk;
    if (kServerCodeConnectError <= server_code && server_code <= kServerCodeInvalidResponse) {
      status = kCallRetError;
    }
    ConsumerApiImpl::UpdateServerResult(connector_->context_, service, *server_, server_code, status, delay);
  }
  // 释放自身
  connector_->async_request_map_.erase(request_id_);
  reactor_.SubmitTask(new DeferDeleteTask<AsyncRequest>(this));
}

AsyncRequestSubmit::~AsyncRequestSubmit() {
  if (request_ != nullptr) {
    delete request_;
    request_ = nullptr;
  }
}

void AsyncRequestSubmit::Run() {
  if (request_->GetTimeLeft() <= 0) {
    request_->GetCallback()(kReturnTimeout, "select polaris server timeout", nullptr);
    return;
  }

  if (!request_->CheckServiceReady()) {
    ThreadLocalReactor().AddTimingTask(new AsyncRequestSubmit(request_, interval_));
    request_ = nullptr;
    return;  // 稍后重试
  }

  if (request_->Submit()) {
    request_ = nullptr;  // 提交成功交出请求
  }
}

}  // namespace polaris
