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

#include "provider/request.h"

#include "logger.h"

namespace polaris {

/// @brief 创建服务注册请求
InstanceRegisterRequest::InstanceRegisterRequest(const std::string& service_namespace,
                                                 const std::string& service_name,
                                                 const std::string& service_token,
                                                 const std::string& host, int port) {
  impl_ = new Impl();
  impl_->SetWithHostPort(service_namespace, service_name, service_token, host, port);
}

InstanceRegisterRequest::~InstanceRegisterRequest() { delete impl_; }

InstanceRegisterRequest::Impl& InstanceRegisterRequest::GetImpl() const { return *impl_; }

void InstanceRegisterRequest::SetFlowId(uint64_t flow_id) { impl_->SetFlowId(flow_id); }

void InstanceRegisterRequest::SetTimeout(uint64_t timeout) { impl_->SetTimeout(timeout); }

void InstanceRegisterRequest::SetVpcId(const std::string& vpc_id) { impl_->SetVpcId(vpc_id); }

void InstanceRegisterRequest::SetProtocol(const std::string& protocol) {
  impl_->protocol_ = protocol;
}

void InstanceRegisterRequest::SetWeight(int weight) { impl_->weight_ = weight; }

void InstanceRegisterRequest::SetPriority(int priority) { impl_->priority_ = priority; }

void InstanceRegisterRequest::SetVersion(const std::string& version) { impl_->version_ = version; }

void InstanceRegisterRequest::SetMetadata(const std::map<std::string, std::string>& metadata) {
  impl_->metadata_ = metadata;
}

void InstanceRegisterRequest::SetHealthCheckFlag(bool health_check_flag) {
  impl_->health_check_flag_ = health_check_flag;
}

void InstanceRegisterRequest::SetHealthCheckType(HealthCheckType health_check_type) {
  impl_->health_check_type_ = health_check_type;
}

void InstanceRegisterRequest::SetTtl(int ttl) { impl_->ttl_ = ttl; }

// 反注册请求
InstanceDeregisterRequest::InstanceDeregisterRequest(const std::string& service_token,
                                                     const std::string& instance_id) {
  impl_ = new Impl();
  impl_->SetWithId(service_token, instance_id);
}

InstanceDeregisterRequest::InstanceDeregisterRequest(const std::string& service_namespace,
                                                     const std::string& service_name,
                                                     const std::string& service_token,
                                                     const std::string& host, int port) {
  impl_ = new Impl();
  impl_->SetWithHostPort(service_namespace, service_name, service_token, host, port);
}

InstanceDeregisterRequest::~InstanceDeregisterRequest() { delete impl_; }

InstanceDeregisterRequest::Impl& InstanceDeregisterRequest::GetImpl() const { return *impl_; }

void InstanceDeregisterRequest::SetVpcId(const std::string& vpc_id) { impl_->SetVpcId(vpc_id); }

void InstanceDeregisterRequest::SetFlowId(uint64_t flow_id) { impl_->SetFlowId(flow_id); }

void InstanceDeregisterRequest::SetTimeout(uint64_t timeout) { impl_->SetTimeout(timeout); }

// 心跳请求
InstanceHeartbeatRequest::InstanceHeartbeatRequest(const std::string& service_token,
                                                   const std::string& instance_id) {
  impl_ = new Impl();
  impl_->SetWithId(service_token, instance_id);
}

InstanceHeartbeatRequest::InstanceHeartbeatRequest(const std::string& service_namespace,
                                                   const std::string& service_name,
                                                   const std::string& service_token,
                                                   const std::string& host, int port) {
  impl_ = new Impl();
  impl_->SetWithHostPort(service_namespace, service_name, service_token, host, port);
}

InstanceHeartbeatRequest::Impl& InstanceHeartbeatRequest::GetImpl() const { return *impl_; }

InstanceHeartbeatRequest::~InstanceHeartbeatRequest() { delete impl_; }

void InstanceHeartbeatRequest::SetVpcId(const std::string& vpc_id) { impl_->SetVpcId(vpc_id); }

void InstanceHeartbeatRequest::SetFlowId(uint64_t flow_id) { impl_->SetFlowId(flow_id); }

void InstanceHeartbeatRequest::SetTimeout(uint64_t timeout) { impl_->SetTimeout(timeout); }

///////////////////////////////////////////////////////////////////////////////
/// request implement

bool ProviderRequestBase::CheckRequest(const char* request_type) const {
  if (service_namespace_.empty()) {
    POLARIS_LOG(LOG_ERROR, "%s instance with empty service namespace", request_type);
    return false;
  }
  if (service_name_.empty()) {
    POLARIS_LOG(LOG_ERROR, "%s instance with empty service name", request_type);
    return false;
  }
  if (service_token_.empty()) {
    POLARIS_LOG(LOG_ERROR, "%s instance with empty service token", request_type);
    return false;
  }
  if (host_.empty()) {
    POLARIS_LOG(LOG_ERROR, "%s instance with empty instance host", request_type);
    return false;
  }
  if (port_ <= 0 || port_ > 65535) {
    POLARIS_LOG(LOG_ERROR, "%s instance with error port, port must in [1, 65535]", request_type);
    return false;
  }
  return true;
}

void InstanceRegisterRequest::Impl::AddMetdata(const std::string& key, const std::string& value) {
  metadata_.insert(std::make_pair(key, value));
}

v1::Instance* InstanceRegisterRequest::Impl::ToPb() const {
  v1::Instance* instance = new v1::Instance();
  instance->mutable_service_token()->set_value(service_token_);
  instance->mutable_namespace_()->set_value(service_namespace_);
  instance->mutable_service()->set_value(service_name_);

  // 设置实例信息  注册不设置 id 和 health status
  instance->mutable_host()->set_value(host_);
  instance->mutable_port()->set_value(static_cast<uint32_t>(port_));
  if (!vpc_id_.empty()) {
    instance->mutable_vpc_id()->set_value(vpc_id_);
  }
  if (!protocol_.empty()) {
    instance->mutable_protocol()->set_value(protocol_);
  }
  if (!version_.empty()) {
    instance->mutable_version()->set_value(version_);
  }
  if (priority_.HasValue()) {
    instance->mutable_priority()->set_value(static_cast<uint32_t>(priority_.Value()));
  }
  if (weight_.HasValue()) {
    instance->mutable_weight()->set_value(static_cast<uint32_t>(weight_.Value()));
  }
  // 设置实例metadata
  if (!metadata_.empty()) {
    google::protobuf::Map<std::string, std::string>* req_metadata = instance->mutable_metadata();
    std::map<std::string, std::string>::const_iterator it;
    for (it = metadata_.begin(); it != metadata_.end(); it++) {
      (*req_metadata)[it->first] = it->second;
    }
  }

  if (health_check_flag_) {  // 设置健康检查信息
    ::v1::HealthCheck* health_check = instance->mutable_health_check();
    health_check->set_type(::v1::HealthCheck_HealthCheckType_HEARTBEAT);
    ::v1::HeartbeatHealthCheck* heartbeat = health_check->mutable_heartbeat();
    if (ttl_.HasValue()) {
      heartbeat->mutable_ttl()->set_value(ttl_.Value());
    }
  }
  return instance;
}

bool InstanceIdentityRequest::CheckRequest(const char* request_type) const {
  if (instance_id_.HasValue()) {
    if (instance_id_.Value().empty()) {
      POLARIS_LOG(LOG_ERROR, "%s instance with empty instance id", request_type);
      return false;
    }
    if (service_token_.empty()) {
      POLARIS_LOG(LOG_ERROR, "%s instance with empty service token", request_type);
      return false;
    }
    return true;
  }
  return ProviderRequestBase::CheckRequest(request_type);
}

v1::Instance* InstanceIdentityRequest::ToPb() const {
  v1::Instance* instance = new v1::Instance();
  instance->mutable_service_token()->set_value(service_token_);
  if (!instance_id_.HasValue()) {
    instance->mutable_namespace_()->set_value(service_namespace_);
    instance->mutable_service()->set_value(service_name_);
    instance->mutable_host()->set_value(host_);
    instance->mutable_port()->set_value(port_);
    if (!vpc_id_.empty()) {
      instance->mutable_vpc_id()->set_value(vpc_id_);
    }
  } else {
    instance->mutable_id()->set_value(instance_id_.Value());
  }
  return instance;
}

}  // namespace polaris
