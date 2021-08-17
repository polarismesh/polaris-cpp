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

#include "plugin/server_connector/server_connector.h"

#include <features.h>
#include <google/protobuf/message.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/wrappers.pb.h>
#include <inttypes.h>
#include <stdlib.h>
#include <v1/client.pb.h>
#include <v1/code.pb.h>
#include <v1/model.pb.h>
#include <v1/request.pb.h>
#include <v1/response.pb.h>
#include <v1/service.pb.h>

#include <sstream>
#include <string>
#include <utility>
#include "api/consumer_api.h"
#include "context_internal.h"
#include "logger.h"
#include "model/model_impl.h"
#include "polaris/accessors.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "polaris/log.h"
#include "polaris/polaris.h"
#include "provider/request.h"
#include "sync/future.h"
#include "utils/time_clock.h"

namespace polaris {

class InstanceDeregisterRequest;
class InstanceHeartbeatRequest;
class InstanceRegisterRequest;

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
                                     ServiceEventHandler* handler) {
  connector_            = connector;
  service_.service_key_ = service_key;
  service_.data_type_   = data_type;
  sync_interval_        = sync_interval;
  handler_              = handler;
}

DiscoverEventTask::~DiscoverEventTask() {
  if (handler_ != NULL) {
    delete handler_;
  }
}

void DiscoverEventTask::Run() {
  connector_->ProcessQueuedListener(this);
  // 重置handler防止析构时被删除
  handler_ = NULL;
}

///////////////////////////////////////////////////////////////////////////////
GrpcServerConnector::GrpcServerConnector()
    : discover_stream_state_(kDiscoverStreamNotInit), context_(NULL), task_thread_id_(0),
      grpc_client_(NULL), discover_stream_(NULL), stream_response_time_(0),
      server_switch_interval_(0), server_switch_state_(kServerSwitchInit), message_used_time_(0),
      request_queue_size_(0), last_cache_version_(0) {}

GrpcServerConnector::~GrpcServerConnector() {
  // 关闭线程
  reactor_.Stop();
  if (task_thread_id_ != 0) {
    pthread_join(task_thread_id_, NULL);
    task_thread_id_ = 0;
  }
  for (std::map<ServiceKeyWithType, ServiceListener>::iterator it = listener_map_.begin();
       it != listener_map_.end(); ++it) {
    delete it->second.handler_;
  }
  if (grpc_client_ != NULL) {
    discover_stream_ = NULL;
    delete grpc_client_;
  }
  context_ = NULL;
}

ReturnCode GrpcServerConnector::InitTimeoutStrategy(Config* config) {
  static const char kConnectTimeoutKey[]          = "connectTimeout";
  static const uint64_t kConnectTimeoutDefault    = 200;
  static const char kConnectTimeoutMaxKey[]       = "connectTimeoutMax";
  static const uint64_t kConnectTimeoutMaxDefault = 1000;
  static const char kConnectTimeoutExpandKey[]    = "connectTimeoutExpand";
  static const float kConnectTimeoutExpandDefaut  = 1.5;
  uint64_t timeout = config->GetMsOrDefault(kConnectTimeoutKey, kConnectTimeoutDefault);
  POLARIS_CHECK(timeout > 0, kReturnInvalidConfig);
  uint64_t max_timeout = config->GetMsOrDefault(kConnectTimeoutMaxKey, kConnectTimeoutMaxDefault);
  POLARIS_CHECK(max_timeout > timeout, kReturnInvalidConfig);
  float expand = config->GetFloatOrDefault(kConnectTimeoutExpandKey, kConnectTimeoutExpandDefaut);
  POLARIS_CHECK(expand > 1.0, kReturnInvalidConfig);
  connect_timeout_.Init(timeout, max_timeout, expand);

  static const char kMessageTimeoutKey[]          = "messageTimeout";
  static const uint64_t kMessageTimeoutDefault    = 1000;
  static const char kMessageTimeoutMaxKey[]       = "messageTimeoutMax";
  static const uint64_t kMessageTimeoutMaxDefault = 10 * 1000;
  static const char kMessageTimeoutExpandKey[]    = "messageTimeoutExpand";
  static const float kMessageTimeoutExpandDefaut  = 2.0;
  timeout = config->GetMsOrDefault(kMessageTimeoutKey, kMessageTimeoutDefault);
  POLARIS_CHECK(timeout > 0, kReturnInvalidConfig);
  max_timeout = config->GetMsOrDefault(kMessageTimeoutMaxKey, kMessageTimeoutMaxDefault);
  POLARIS_CHECK(max_timeout > timeout, kReturnInvalidConfig);
  expand = config->GetFloatOrDefault(kMessageTimeoutExpandKey, kMessageTimeoutExpandDefaut);
  POLARIS_CHECK(expand > 1.0, kReturnInvalidConfig);
  message_timeout_.Init(timeout, max_timeout, expand);
  return kReturnOk;
}

ReturnCode GrpcServerConnector::Init(Config* config, Context* context) {
  static const char kServerAddressesKey[] = "addresses";
  static const char kJoinPointKey[]       = "joinPoint";
  static const char kServerDefault[]      = "default";

  static const char kServerSwitchIntervalKey[]  = "serverSwitchInterval";
  static const int kServerSwitchIntervalDefault = 10 * 60 * 1000;

  static const char kMaxRequestQueueSizeKey[]  = "requestQueueSize";
  static const int kMaxRequestQueueSizeDefault = 1000;

  context_ = context;

  // 先获取接入点
  SeedServerConfig& seed_config = context_->GetContextImpl()->GetSeedConfig();
  std::string join_point        = config->GetStringOrDefault(kJoinPointKey, "");
  if (!join_point.empty()) {
    if (seed_config.UpdateJoinPoint(join_point) != kReturnOk) {
      POLARIS_LOG(LOG_ERROR, "unkown join point %s", join_point.c_str());
      return kReturnInvalidConfig;
    }
  }
  // 获取埋点地址配置
  std::vector<std::string> config_server =
      config->GetListOrDefault(kServerAddressesKey, kServerDefault);
  if (config_server.empty()) {
    POLARIS_LOG(LOG_ERROR, "get polaris server address failed");
    return kReturnInvalidConfig;
  } else if (config_server.size() == 1 && config_server[0] == kServerDefault) {
    seed_config.GetSeedServer(server_lists_);
  } else if (SeedServerConfig::ParseSeedServer(config_server, server_lists_) == 0) {
    POLARIS_LOG(LOG_ERROR, "parse polaris server address failed");
    return kReturnInvalidConfig;
  }

  server_switch_interval_ = config->GetMsOrDefault(kServerSwitchIntervalKey,
                                                   kServerSwitchIntervalDefault);  // default 10m
  POLARIS_CHECK(server_switch_interval_ >= 60 * 1000, kReturnInvalidConfig);

  if (InitTimeoutStrategy(config) != kReturnOk) {
    return kReturnInvalidConfig;
  }

  int request_queue_size =
      config->GetIntOrDefault(kMaxRequestQueueSizeKey, kMaxRequestQueueSizeDefault);
  POLARIS_CHECK(request_queue_size > 0, kReturnInvalidConfig);
  request_queue_size_ = static_cast<std::size_t>(request_queue_size);

  POLARIS_LOG(LOG_INFO, "seed server list:%s",
              SeedServerConfig::SeedServersToString(server_lists_).c_str());

  // 创建任务执行线程
  if (task_thread_id_ == 0) {
    if (pthread_create(&task_thread_id_, NULL, ThreadFunction, this) != 0) {
      POLARIS_LOG(LOG_ERROR, "create server connector task thread error");
      return kReturnInvalidState;
    }
#if defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 12)
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
  return NULL;
}

ReturnCode GrpcServerConnector::RegisterEventHandler(const ServiceKey& service_key,
                                                     ServiceDataType data_type,
                                                     uint64_t sync_interval,
                                                     ServiceEventHandler* handler) {
  if (handler == NULL) {
    POLARIS_LOG(LOG_ERROR,
                "register event handler for service:[%s/%s] failed since handler is null",
                service_key.namespace_.c_str(), service_key.name_.c_str());
    return kReturnInvalidArgument;
  }
  reactor_.SubmitTask(new DiscoverEventTask(this, service_key, data_type, sync_interval, handler));
  reactor_.Notify();
  POLARIS_LOG(LOG_INFO, "register %s event handler for service[%s/%s]", DataTypeToStr(data_type),
              service_key.namespace_.c_str(), service_key.name_.c_str());
  return kReturnOk;
}

ReturnCode GrpcServerConnector::DeregisterEventHandler(const ServiceKey& service_key,
                                                       ServiceDataType data_type) {
  reactor_.SubmitTask(new DiscoverEventTask(this, service_key, data_type, 0, NULL));
  reactor_.Notify();
  POLARIS_LOG(LOG_INFO, "deregister %s event handler for service[%s/%s]", DataTypeToStr(data_type),
              service_key.namespace_.c_str(), service_key.name_.c_str());
  return kReturnOk;
}

void GrpcServerConnector::ProcessQueuedListener(DiscoverEventTask* discover_event) {
  POLARIS_ASSERT(discover_event->connector_ == this);
  std::map<ServiceKeyWithType, ServiceListener>::iterator service_it =
      listener_map_.find(discover_event->service_);
  if (discover_event->handler_ == NULL) {  // 服务缓存过期的情况下反注册服务监听
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
                                             service_listener.service_.data_type_, NULL);
    delete service_listener.handler_;
    // 监听map中删除，这样如果服务应答了相关数据找不到监听直接丢弃即可
    listener_map_.erase(service_it);
  } else {
    POLARIS_ASSERT(service_it == listener_map_.end());  // 不能重复注册监听
    ServiceListener& service_listener    = listener_map_[discover_event->service_];
    service_listener.service_            = discover_event->service_;
    service_listener.sync_interval_      = discover_event->sync_interval_;
    service_listener.handler_            = discover_event->handler_;
    service_listener.cache_version_      = 0;
    service_listener.ret_code_           = 0;
    service_listener.discover_task_iter_ = reactor_.TimingTaskEnd();
    service_listener.timeout_task_iter_  = reactor_.TimingTaskEnd();
    service_listener.connector_          = this;
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
  POLARIS_ASSERT(grpc_client_ != NULL);
  if (discover_stream_ == NULL) {  // 检查stream，不正常返回并放入等待队列
    POLARIS_LOG(LOG_TRACE, "server connector pending discover %s request for service[%s/%s]",
                DataTypeToStr(service_listener.service_.data_type_), service_key.namespace_.c_str(),
                service_key.name_.c_str());
    return false;
  }
  if (discover_stream_state_ != kDiscoverStreamInit) {
    const ServiceKey& discover_service = context_->GetContextImpl()->GetDiscoverService().service_;
    if (service_key.name_ != discover_service.name_ ||
        service_key.namespace_ != discover_service.namespace_) {
      POLARIS_LOG(LOG_INFO, "wait discover service before discover %s for service[%s/%s]",
                  DataTypeToStr(service_listener.service_.data_type_),
                  service_key.namespace_.c_str(), service_key.name_.c_str());
      return false;
    }
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
  service_listener.timeout_task_iter_ = reactor_.AddTimingTask(new TimingFuncTask<ServiceListener>(
      DiscoverTimoutCheck, &service_listener, message_timeout_.GetTimeout()));
  return true;
}

void GrpcServerConnector::DiscoverTimoutCheck(ServiceListener* service_listener) {
  // 超时的情况下一定是未反注册的，那么这里需要触发切换
  // 这里一个流上只要有第一个请求超时了，那么触发切换就会取消其他已发送未应答的服务的超时检查任务
  service_listener->timeout_task_iter_ = service_listener->connector_->reactor_.TimingTaskEnd();
  const ServiceKey& service_key        = service_listener->service_.service_key_;
  POLARIS_LOG(LOG_INFO, "server switch because discover [%s/%s] timeout[%" PRIu64 "]",
              service_key.namespace_.c_str(), service_key.name_.c_str(),
              service_listener->connector_->message_timeout_.GetTimeout());
  service_listener->connector_->message_timeout_.SetNextRetryTimeout();
  // 加入pending等待连接成功后发送
  service_listener->connector_->pending_for_connected_.insert(service_listener);
  service_listener->connector_->UpdateCallResult(
      kServerCodeRpcTimeout, service_listener->connector_->message_timeout_.GetTimeout());
  service_listener->connector_->ServerSwitch();
}

bool GrpcServerConnector::CompareVersion(ServiceListener& listener,
                                         const ::v1::DiscoverResponse& response) {
  if (response.code().value() == v1::DataNoChange) {
    return false;
  }
  const ::v1::Service& resp_service = response.service();
  if (listener.ret_code_ == response.code().value()) {  // code不变的情形下才比较具体的值
    if (resp_service.has_revision() &&
        !resp_service.revision().value().empty()) {  // revision有效才比较是否相同
      if (resp_service.revision().value() == listener.revision_) {
        return false;
      }
    } else {
      // revision无效，未配置或被删除，如果本地revision为空且不是首次更新说明已经更新过数据
      if (listener.revision_.empty() && listener.cache_version_ > 0) {
        return false;
      }
    }
  }
  listener.revision_      = resp_service.revision().value();
  listener.cache_version_ = ++last_cache_version_;
  listener.ret_code_      = response.code().value();
  return true;
}

void GrpcServerConnector::OnReceiveMessage(v1::DiscoverResponse* response) {
  stream_response_time_         = Time::GetCurrentTimeMs();
  PolarisServerCode server_code = ToPolarisServerCode(response->code().value());
  if (server_code == kServerCodeReturnOk ||
      (server_code == kServerCodeInvalidRequest &&
       ToClientReturnCode(response->code()) == kReturnServiceNotFound)) {
    ProcessDiscoverResponse(*response);
  } else {
    POLARIS_LOG(LOG_ERROR, "discover stream response with server error:%d-%s",
                response->code().value(), response->info().value().c_str());
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
  service_with_type.service_key_.name_      = resp_service.name().value();
  const ServiceKey& service_key             = service_with_type.service_key_;
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
                service_key.namespace_.c_str(), service_key.name_.c_str(),
                static_cast<int>(response.type()));
    this->UpdateCallResult(kServerCodeInvalidResponse, 0);
    return kReturnOk;
  }
  // 查找服务监听，处理应答
  std::map<ServiceKeyWithType, ServiceListener>::iterator service_it =
      listener_map_.find(service_with_type);
  if (service_it == listener_map_.end()) {
    POLARIS_LOG(LOG_INFO, "discover %s for service[%s/%s], but handler was deregister",
                DataTypeToStr(service_with_type.data_type_), service_key.namespace_.c_str(),
                service_key.name_.c_str());
    return kReturnOk;
  }

  uint64_t delay            = 0;
  ServiceListener& listener = service_it->second;
  // 处理超时检查请求
  if (listener.timeout_task_iter_ != reactor_.TimingTaskEnd()) {
    delay = Time::GetCurrentTimeMs() + message_timeout_.GetTimeout();
    delay =
        delay > listener.timeout_task_iter_->first ? delay - listener.timeout_task_iter_->first : 0;
    reactor_.CancelTimingTask(listener.timeout_task_iter_);
    listener.timeout_task_iter_ = reactor_.TimingTaskEnd();
  }

  ReturnCode ret = ToClientReturnCode(response.code());
  if (ret == kReturnOk || ret == kReturnServiceNotFound) {  // 返回NotFound的时候也需要触发Handler
    this->UpdateCallResult(kServerCodeReturnOk, delay);
    this->UpdateMaxUsedTime(delay);  // 更新最大discover请求耗时
    if (CompareVersion(listener, response)) {
      ServiceData* event_data = ServiceData::CreateFromPb(
          reinterpret_cast<void*>(&response), ret == kReturnOk ? kDataIsSyncing : kDataNotFound,
          listener.cache_version_);
      // 执行回调
      listener.handler_->OnEventUpdate(service_key, event_data->GetDataType(), event_data);
      if (ret == kReturnOk && discover_stream_state_ < kDiscoverStreamGetInstance) {
        const ServiceKey discover_service =
            context_->GetContextImpl()->GetDiscoverService().service_;
        if (service_with_type.data_type_ == kServiceDataInstances &&
            service_key == discover_service) {
          server_switch_state_   = kServerSwitchDefault;
          discover_stream_state_ = kDiscoverStreamGetInstance;
          reactor_.CancelTimingTask(server_switch_task_iter_);  // 取消定时切换任务，触发立即切换
          server_switch_task_iter_ = reactor_.AddTimingTask(new TimingFuncTask<GrpcServerConnector>(
              GrpcServerConnector::TimingServerSwitch, this, 0));
        }
        POLARIS_LOG(LOG_INFO, "discover service %s for service[%s/%s]",
                    DataTypeToStr(service_with_type.data_type_), service_key.namespace_.c_str(),
                    service_key.name_.c_str());
      }
    } else {
      listener.handler_->OnEventSync(service_key, listener.service_.data_type_);
      if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
        POLARIS_LOG(LOG_TRACE,
                    "skip update %s for service[%s/%s] because of same revision[%s] and code: %d",
                    DataTypeToStr(service_with_type.data_type_), service_key.namespace_.c_str(),
                    service_key.name_.c_str(), resp_service.revision().value().c_str(),
                    response.code().value());
      }
    }
  } else {
    this->UpdateCallResult(kServerCodeInvalidRequest, delay);
    POLARIS_LOG(LOG_ERROR, "discover %s for service[%s/%s] with server error[%" PRId32 ":%s]",
                DataTypeToStr(service_with_type.data_type_), service_key.namespace_.c_str(),
                service_key.name_.c_str(), response.code().value(),
                response.info().value().c_str());
  }
  // 设置下一次的检查任务
  listener.discover_task_iter_ = reactor_.AddTimingTask(
      new TimingFuncTask<ServiceListener>(TimingDiscover, &listener, listener.sync_interval_));
  return kReturnOk;
}

void GrpcServerConnector::OnRemoteClose(grpc::GrpcStatusCode status, const std::string& message) {
  POLARIS_LOG(LOG_ERROR, "discover stream close by remote with error:%d-%s", status,
              message.c_str());
  // 触发server切换，所有超时检查会取消，并重新等待连接连接后重新发送
  this->UpdateCallResult(kServerCodeServerError, connect_timeout_.GetTimeout());
  this->ServerSwitch();
}

void GrpcServerConnector::TimingServerSwitch(GrpcServerConnector* server_connector) {
  POLARIS_ASSERT(server_connector != NULL);
  if (server_connector->server_switch_state_ == kServerSwitchNormal) {  // 正常状态下
    server_connector->server_switch_state_ = kServerSwitchPeriodic;
    POLARIS_LOG(LOG_INFO, "switch from server[%s] with timing[%" PRIu64 "]",
                server_connector->grpc_client_->CurrentServer().c_str(),
                server_connector->server_switch_interval_);
    // 根据上一个连接使用的消息耗时重置消息超时时间
    if (server_connector->message_used_time_ > 0) {
      server_connector->message_timeout_.SetNormalTimeout(server_connector->message_used_time_);
      server_connector->message_used_time_ = 0;
    }
    server_connector->UpdateCallResult(kServerCodeReturnOk, 0);
  } else if (server_connector->server_switch_state_ == kServerSwitchDefault) {
    server_connector->server_switch_state_ = kServerSwitchPeriodic;
    POLARIS_LOG(LOG_INFO, "switch from default server[%s] to seed service",
                server_connector->grpc_client_->CurrentServer().c_str());
    server_connector->UpdateCallResult(kServerCodeReturnOk, 0);
  } else if (server_connector->server_switch_state_ == kServerSwitchBegin) {
    server_connector->server_switch_state_ = kServerSwitchTimeout;
    POLARIS_LOG(LOG_INFO, "switch from server[%s] with connect timeout[%" PRIu64 "]",
                server_connector->grpc_client_->CurrentServer().c_str(),
                server_connector->connect_timeout_.GetTimeout());
    server_connector->connect_timeout_.SetNextRetryTimeout();  // 扩张连接超时时间
    server_connector->UpdateCallResult(kServerCodeConnectError,
                                       server_connector->connect_timeout_.GetTimeout());
  }
  server_connector->ServerSwitch();
}

ReturnCode GrpcServerConnector::SelectInstance(const ServiceKey& service_key, uint32_t timeout,
                                               Instance** instance, bool ignore_half_open) {
  Criteria criteria;
  criteria.ignore_half_open_ = ignore_half_open;
  ReturnCode retCode =
      ConsumerApiImpl::GetSystemServer(context_, service_key, criteria, *instance, timeout);
  if (retCode == kReturnSystemServiceNotConfigured) {
    SeedServer& server = server_lists_[rand() % server_lists_.size()];
    *instance          = new Instance("", server.ip_, server.port_, 100);
    return kReturnOk;
  }
  return retCode;
}

void GrpcServerConnector::ServerSwitch() {
  if (server_switch_state_ == kServerSwitchNormal       // 服务调用出错或超时触发切换
      || server_switch_state_ == kServerSwitchBegin) {  // 切换后异步连接回调触发重新
    reactor_.CancelTimingTask(server_switch_task_iter_);
  }

  // 选择一个服务器
  std::string host;
  int port = 0;
  if (discover_stream_state_ >= kDiscoverStreamGetInstance) {  // 说明内部服务已经返回
    Instance* instance                 = NULL;
    const ServiceKey& discover_service = context_->GetContextImpl()->GetDiscoverService().service_;
    bool ignore_half_open = server_switch_state_ != kServerSwitchPeriodic;  // 周期切换才选半开节点
    ReturnCode ret_code = SelectInstance(discover_service, 0, &instance, ignore_half_open);
    if (ret_code == kReturnOk) {
      POLARIS_ASSERT(instance != NULL);
      host               = instance->GetHost();
      port               = instance->GetPort();
      discover_instance_ = instance->GetId();
      delete instance;
      discover_stream_state_ = kDiscoverStreamInit;
      POLARIS_LOG(LOG_INFO, "discover stream switch to discover server[%s:%d]", host.c_str(), port);
    } else {
      POLARIS_LOG(LOG_ERROR, "discover polaris service[%s/%s] return error[%s]",
                  discover_service.namespace_.c_str(), discover_service.name_.c_str(),
                  ReturnCodeToMsg(ret_code).c_str());
      server_switch_state_     = kServerSwitchNormal;  // 设置成正常状态，稍后重试
      server_switch_task_iter_ = reactor_.AddTimingTask(new TimingFuncTask<GrpcServerConnector>(
          GrpcServerConnector::TimingServerSwitch, this, message_timeout_.GetTimeout()));
      return;
    }
  } else {
    SeedServer& server = server_lists_[rand() % server_lists_.size()];
    host               = server.ip_;
    port               = server.port_;
    // 如果没有配置discover服务名，那么则此时已经初始化完成
    if (context_->GetContextImpl()->GetDiscoverService().service_.name_.empty()) {
      discover_stream_state_ = kDiscoverStreamInit;
    }
    POLARIS_LOG(LOG_INFO, "discover stream switch to inner server[%s:%d]", host.c_str(), port);
  }

  // 有超时检查的服务，说明本次未完成服务发现，加入pending列表，从而可在切换成功后立马发送
  for (std::map<ServiceKeyWithType, ServiceListener>::iterator it = listener_map_.begin();
       it != listener_map_.end(); ++it) {
    if (it->second.timeout_task_iter_ != reactor_.TimingTaskEnd()) {
      reactor_.CancelTimingTask(it->second.timeout_task_iter_);
      pending_for_connected_.insert(&it->second);
      it->second.timeout_task_iter_ = reactor_.TimingTaskEnd();
    }
  }

  // 设置定时任务进行超时检查
  server_switch_state_     = kServerSwitchBegin;
  server_switch_task_iter_ = reactor_.AddTimingTask(new TimingFuncTask<GrpcServerConnector>(
      TimingServerSwitch, this, connect_timeout_.GetTimeout()));
  if (grpc_client_ != NULL) {  // 删除旧连接客户端
    discover_stream_ = NULL;   // 重置stream为NULL
    delete grpc_client_;
  }
  grpc_client_ = new grpc::GrpcClient(reactor_);
  grpc_client_->ConnectTo(host, port, connect_timeout_.GetTimeout(),
                          new DiscoverConnectionCb(this));
}

void GrpcServerConnector::UpdateCallResult(PolarisServerCode server_code, uint64_t delay) {
  if (discover_instance_.empty()) {
    return;  // 内部埋点实例 不要上报
  }
  InstanceGauge instance_gauge;
  const ServiceKey& service        = context_->GetContextImpl()->GetDiscoverService().service_;
  instance_gauge.service_namespace = service.namespace_;
  instance_gauge.service_name      = service.name_;
  instance_gauge.instance_id       = discover_instance_;
  instance_gauge.call_daley        = delay;
  instance_gauge.call_ret_code     = server_code;
  if (kServerCodeConnectError <= server_code && server_code <= kServerCodeInvalidResponse) {
    if (server_code == kServerCodeRpcTimeout &&
        stream_response_time_ + delay > Time::GetCurrentTimeMs()) {
      instance_gauge.call_ret_status = kCallRetOk;
    } else {  // 消息超时且stream上超时时间内没有数据返回则上报失败
      instance_gauge.call_ret_status = kCallRetError;
    }
  } else {
    instance_gauge.call_ret_status = kCallRetOk;
  }
  ConsumerApiImpl::UpdateServiceCallResult(context_, instance_gauge);
}

BlockRequest* GrpcServerConnector::CreateBlockRequest(BlockRequestType request_type,
                                                      uint64_t timeout) {
  return new BlockRequest(request_type, *this, timeout);
}

ReturnCode GrpcServerConnector::RegisterInstance(const InstanceRegisterRequest& req,
                                                 uint64_t timeout_ms, std::string& instance_id) {
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
    ret_code               = ToClientReturnCode(response->code());
    if (ret_code == kReturnOk || ret_code == kReturnExistedResource) {
      instance_id = response->instance().id().value();
    }
    delete response;
  }
  delete future;
  return ret_code;
}

ReturnCode GrpcServerConnector::DeregisterInstance(const InstanceDeregisterRequest& req,
                                                   uint64_t timeout_ms) {
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
    ret_code               = ToClientReturnCode(response->code());
    delete response;
  }
  delete future;
  return ret_code;
}

ReturnCode GrpcServerConnector::InstanceHeartbeat(const InstanceHeartbeatRequest& req,
                                                  uint64_t timeout_ms) {
  if (timeout_ms == 0) {
    return kReturnInvalidArgument;
  }
  BlockRequest* block_request = CreateBlockRequest(kBlockHeartbeat, timeout_ms);
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
    ret_code               = ToClientReturnCode(response->code());
    delete response;
  }
  delete future;
  return ret_code;
}

ReturnCode GrpcServerConnector::ReportClient(const std::string& host, uint64_t timeout_ms,
                                             Location& location) {
  if (host.empty()) {
    return kReturnInvalidArgument;
  }
  BlockRequest* block_request = CreateBlockRequest(kBlockReportClient, timeout_ms);
  if (!block_request->PrepareClient()) {
    delete block_request;
    return kReturnNetworkFailed;
  }

  v1::Client* client = new v1::Client();
  client->mutable_host()->set_value(host);
  client->mutable_version()->set_value(polaris::GetVersion());
  client->set_type(v1::Client_ClientType_SDK);

  Future<v1::Response>* future = block_request->SendRequest(client);
  if (!future->Wait(block_request->GetTimeout()) || !future->IsReady()) {
    delete future;
    return kReturnTimeout;
  }
  ReturnCode ret_code = kReturnOk;
  if (future->IsFailed()) {
    ret_code = future->GetError();
  } else {
    v1::Response* response = future->GetValue();
    if ((ret_code = ToClientReturnCode(response->code())) == kReturnOk) {
      if (response->has_client() && response->client().has_location()) {
        const v1::Location& client_location = response->client().location();
        location.region                     = client_location.region().value();
        location.zone                       = client_location.zone().value();
        location.campus                     = client_location.campus().value();
      } else {
        ret_code = kReturnResourceNotFound;
      }
    }
    delete response;
  }
  delete future;
  return ret_code;
}

bool GrpcServerConnector::GetInstance(BlockRequest* block_request) {
  POLARIS_ASSERT(block_request != NULL);
  POLARIS_ASSERT(block_request->instance_ == NULL);
  const PolarisCluster& system_cluster = block_request->request_type_ == kBlockHeartbeat
                                             ? context_->GetContextImpl()->GetHeartbeatService()
                                             : context_->GetContextImpl()->GetDiscoverService();
  ReturnCode ret_code = SelectInstance(system_cluster.service_, block_request->request_timeout_,
                                       &block_request->instance_);
  if (ret_code == kReturnOk) {
    POLARIS_ASSERT(block_request->instance_ != NULL);
    POLARIS_LOG(LOG_DEBUG, "get server:%s:%d for %s", block_request->instance_->GetHost().c_str(),
                block_request->instance_->GetPort(), block_request->RequestTypeToStr());
    return true;
  } else {
    POLARIS_ASSERT(block_request->instance_ == NULL);
    POLARIS_LOG(LOG_ERROR, "get server for %s with error:%s", block_request->RequestTypeToStr(),
                ReturnCodeToMsg(ret_code).c_str());
  }
  return false;
}

void GrpcServerConnector::UpdateCallResult(BlockRequest* block_request) {
  POLARIS_ASSERT(block_request->instance_ != NULL);
  InstanceGauge instance_gauge;
  const PolarisCluster& system_cluster = block_request->request_type_ == kBlockHeartbeat
                                             ? context_->GetContextImpl()->GetHeartbeatService()
                                             : context_->GetContextImpl()->GetDiscoverService();
  instance_gauge.service_namespace     = system_cluster.service_.namespace_;
  instance_gauge.service_name          = system_cluster.service_.name_;
  instance_gauge.instance_id           = block_request->instance_->GetId();
  delete block_request->instance_;
  block_request->instance_ = NULL;
  if (instance_gauge.instance_id.empty()) {
    return;  // 内部埋点实例 不要上报
  }
  instance_gauge.call_daley     = Time::GetCurrentTimeMs() - block_request->call_begin_;
  instance_gauge.call_ret_code  = block_request->server_code_;
  PolarisServerCode server_code = block_request->server_code_;
  if (kServerCodeConnectError <= server_code && server_code <= kServerCodeInvalidResponse) {
    instance_gauge.call_ret_status = kCallRetError;
  } else {
    instance_gauge.call_ret_status = kCallRetOk;
  }
  ConsumerApiImpl::UpdateServiceCallResult(context_, instance_gauge);
}

///////////////////////////////////////////////////////////////////////////////
DiscoverConnectionCb::DiscoverConnectionCb(GrpcServerConnector* connector)
    : connector_(connector), connect_time_(Time::GetCurrentTimeMs()) {}

DiscoverConnectionCb::~DiscoverConnectionCb() { connector_ = NULL; }

void DiscoverConnectionCb::OnSuccess() {
  // 用当前连接建立耗时设置下一次连接超时时间
  uint64_t connect_used_time = Time::GetCurrentTimeMs() - connect_time_;
  connector_->connect_timeout_.SetNormalTimeout(connect_used_time);
  POLARIS_ASSERT(connector_->grpc_client_ != NULL);
  POLARIS_ASSERT(connector_->discover_stream_ == NULL);
  POLARIS_ASSERT(connector_->server_switch_state_ == kServerSwitchBegin);
  connector_->server_switch_state_ = kServerSwitchNormal;
  connector_->reactor_.CancelTimingTask(connector_->server_switch_task_iter_);  // 取消超时检查
  // 设置正常的周期切换
  connector_->server_switch_task_iter_ = connector_->reactor_.AddTimingTask(
      new TimingFuncTask<GrpcServerConnector>(GrpcServerConnector::TimingServerSwitch, connector_,
                                              connector_->server_switch_interval_));
  POLARIS_LOG(LOG_INFO,
              "connect to server[%s] used[%" PRIu64 "], send %zu pending discover request",
              connector_->grpc_client_->CurrentServer().c_str(), connect_used_time,
              connector_->pending_for_connected_.size());
  // 创建stream 发起pending的请求
  static const char kDiscoverPath[] = "/v1.PolarisGRPC/Discover";
  connector_->discover_stream_ = connector_->grpc_client_->StartStream(kDiscoverPath, *connector_);
  connector_->stream_response_time_ = Time::GetCurrentTimeMs();
  std::set<ServiceListener*> need_pending;
  std::set<ServiceListener*>& pending = connector_->pending_for_connected_;
  for (std::set<ServiceListener*>::iterator it = pending.begin(); it != pending.end(); ++it) {
    if (!connector_->SendDiscoverRequest(**it)) {
      need_pending.insert(*it);
    }
  }
  pending.swap(need_pending);
}

void DiscoverConnectionCb::OnFailed() {
  POLARIS_LOG(LOG_INFO, "connect to server[%s] callback failed",
              connector_->grpc_client_->CurrentServer().c_str());
  POLARIS_ASSERT(connector_->grpc_client_ != NULL);
  POLARIS_ASSERT(connector_->discover_stream_ == NULL);
  POLARIS_ASSERT(connector_->server_switch_state_ == kServerSwitchBegin);
  // 这里不直接触发重新切换Server，让链接超时检查去切换
}

void DiscoverConnectionCb::OnTimeout() {
  POLARIS_LOG(LOG_INFO, "connect to server[%s] callback timeout",
              connector_->grpc_client_->CurrentServer().c_str());
  // 这里不直接触发重新切换Server，让链接超时检查去切换
}

///////////////////////////////////////////////////////////////////////////////
BlockRequest::BlockRequest(BlockRequestType request_type, GrpcServerConnector& connector,
                           uint64_t request_timeout)
    : request_type_(request_type), connector_(connector), request_timeout_(request_timeout),
      server_code_(kServerCodeReturnOk), call_begin_(Time::GetCurrentTimeMs()), message_(NULL),
      promise_(NULL), instance_(NULL), grpc_client_(NULL) {}

BlockRequest::~BlockRequest() {
  if (instance_ != NULL) {
    delete instance_;
    instance_ = NULL;
  }
  if (message_ != NULL) {
    delete message_;
    message_ = NULL;
  }
  if (promise_ != NULL) {
    delete promise_;
    promise_ = NULL;
  }
  if (grpc_client_ != NULL) {
    delete grpc_client_;
    grpc_client_ = NULL;
  }
}

const char* BlockRequest::GetCallPath() {
  switch (request_type_) {
    case kBlockRegisterInstance:
      return "/v1.PolarisGRPC/RegisterInstance";
    case kBlockDeregisterInstance:
      return "/v1.PolarisGRPC/DeregisterInstance";
    case kBlockHeartbeat:
      return "/v1.PolarisGRPC/Heartbeat";
    case kBlockReportClient:
      return "/v1.PolarisGRPC/ReportClient";
    default:
      POLARIS_ASSERT(false);
  }
}

const char* BlockRequest::RequestTypeToStr() {
  switch (request_type_) {
    case kBlockRegisterInstance:
      return "RegisterInstance";
    case kBlockDeregisterInstance:
      return "DeregisterInstance";
    case kBlockHeartbeat:
      return "Heartbeat";
    case kBlockReportClient:
      return "ReportClient";
    default:
      POLARIS_ASSERT(false);
  }
}

void BlockRequest::OnSuccess(::v1::Response* response) {
  // RPC调用正常，且请求处理执行正确
  server_code_ = ToPolarisServerCode(response->code().value());
  if (server_code_ != kServerCodeServerError) {
    if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
      POLARIS_LOG(LOG_TRACE, "%s for request[%s] to server[%s:%d] success with response[%s]",
                  RequestTypeToStr(), message_->ShortDebugString().c_str(),
                  instance_->GetHost().c_str(), instance_->GetPort(),
                  response->ShortDebugString().c_str());
    }
    promise_->SetValue(response);
  } else {  // 请求处理失败
    promise_->SetError(kReturnServerError);
    POLARIS_LOG(LOG_ERROR, "%s for request[%s] to server[%s:%d] error with response[%s]",
                RequestTypeToStr(), message_->ShortDebugString().c_str(),
                instance_->GetHost().c_str(), instance_->GetPort(),
                response->ShortDebugString().c_str());
    delete response;
  }
}

void BlockRequest::OnFailure(grpc::GrpcStatusCode status, const std::string& message) {
  POLARIS_ASSERT(status != grpc::kGrpcStatusOk);  // RPC 调用错误
  POLARIS_LOG(LOG_ERROR, "%s for request[%s] to server[%s:%d] with rpc error %s",
              RequestTypeToStr(), message_->ShortDebugString().c_str(),
              instance_->GetHost().c_str(), instance_->GetPort(), message.c_str());
  ReturnCode ret_code;
  if (status == grpc::kGrpcStatusDeadlineExceeded) {
    ret_code     = kReturnTimeout;
    server_code_ = kServerCodeRpcTimeout;
  } else {
    ret_code     = kReturnNetworkFailed;
    server_code_ = kServerCodeRpcError;
  }
  promise_->SetError(ret_code);
}

bool BlockRequest::PrepareClient() {
  uint64_t begin_time = Time::GetCurrentTimeMs();
  if (!connector_.GetInstance(this)) {  // 选择服务实例
    return false;
  }

  // 建立grpc客户端，并尝试连接
  grpc_client_ = new grpc::GrpcClient(connector_.GetReactor());
  if (!grpc_client_->ConnectTo(instance_->GetHost(), instance_->GetPort()) ||
      !grpc_client_->WaitConnected(request_timeout_)) {
    server_code_ = kServerCodeConnectError;
    connector_.UpdateCallResult(this);
    return false;
  }
  uint64_t use_time = Time::GetCurrentTimeMs() - begin_time;
  if (use_time >= request_timeout_) {
    server_code_ = kServerCodeConnectError;
    connector_.UpdateCallResult(this);
    return false;
  }
  request_timeout_ -= use_time;
  return true;
}

Future<v1::Response>* BlockRequest::SendRequest(google::protobuf::Message* message) {
  POLARIS_ASSERT(message != NULL);
  POLARIS_ASSERT(message_ == NULL);
  POLARIS_ASSERT(promise_ == NULL);
  message_ = message;
  promise_ = new Promise<v1::Response>();
  connector_.GetReactor().SubmitTask(new BlockRequestTask(this));
  return promise_->GetFuture();
}

///////////////////////////////////////////////////////////////////////////////
BlockRequestTask::BlockRequestTask(BlockRequest* request) : request_(request) {}

BlockRequestTask::~BlockRequestTask() {
  if (request_ != NULL) {  // 说明任务未执行，还未提交超时检查任务，在此处释放
    delete request_;
    request_ = NULL;
  }
}

void BlockRequestTask::Run() {
  POLARIS_ASSERT(request_->promise_ != NULL);
  POLARIS_ASSERT(request_->grpc_client_ != NULL);
  request_->grpc_client_->SubmitToReactor();  // 把连接建立成功的http2client加入event loop
  request_->grpc_client_->SendRequest(*request_->message_, request_->GetCallPath(),
                                      request_->request_timeout_, *request_);
  // 提交超时检查
  request_->connector_.GetReactor().AddTimingTask(
      new BlockRequestTimeout(request_, request_->request_timeout_));
  request_ = NULL;  // request交给超时检查任务释放
}

///////////////////////////////////////////////////////////////////////////////
BlockRequestTimeout::BlockRequestTimeout(BlockRequest* request, uint64_t timeout)
    : TimingTask(timeout), request_(request) {
  request_->server_code_ = kServerCodeRpcTimeout;
}

BlockRequestTimeout::~BlockRequestTimeout() {
  POLARIS_ASSERT(request_ != NULL);
  delete request_;  // 只能在最后超时的时候释放
  request_ = NULL;
}

void BlockRequestTimeout::Run() { request_->connector_.UpdateCallResult(request_); }

}  // namespace polaris
