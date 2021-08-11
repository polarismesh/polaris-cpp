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

#include "model/requests.h"

#include <stddef.h>
#include "model/constants.h"
#include "polaris/accessors.h"
#include "polaris/consumer.h"
#include "polaris/provider.h"

namespace polaris {

// 全局的空字符串map
const std::map<std::string, std::string>& EmptyStringMap() {
  static std::map<std::string, std::string> EMPTY_STRING_MAP;
  return EMPTY_STRING_MAP;
}

// 获取单个服务请求
GetOneInstanceRequest::GetOneInstanceRequest(const ServiceKey& service_key) {
  impl               = new GetOneInstanceRequestImpl();
  impl->service_key_ = service_key;
}

GetOneInstanceRequest::~GetOneInstanceRequest() { delete impl; }

void GetOneInstanceRequest::SetHashKey(uint64_t hash_key) { impl->criteria_.hash_key_ = hash_key; }

void GetOneInstanceRequest::SetHashString(const std::string& hash_string) {
  impl->criteria_.hash_string_ = hash_string;
}

void GetOneInstanceRequest::SetIgnoreHalfOpen(bool ignore_half_open) {
  impl->criteria_.ignore_half_open_ = ignore_half_open;
}

void GetOneInstanceRequest::SetSourceService(const ServiceInfo& source_service) {
  if (impl->source_service_.IsNull()) {
    impl->source_service_.Set(new ServiceInfo(source_service));
  } else {
    *impl->source_service_ = source_service;
  }
}

bool GetOneInstanceRequest::SetSourceSetName(const std::string& set_name) {
  if (impl->source_service_.IsNull()) {
    impl->source_service_.Set(new ServiceInfo());
  }
  impl->source_service_->metadata_[constants::kRouterRequestSetNameKey] = set_name;
  return true;
}

void GetOneInstanceRequest::SetCanary(const std::string& canary) {
  if (impl->source_service_.IsNull()) {
    impl->source_service_.Set(new ServiceInfo());
  }
  impl->source_service_->metadata_[constants::kRouterRequestCanaryKey] = canary;
}

void GetOneInstanceRequest::SetFlowId(uint64_t flow_id) { impl->flow_id_ = flow_id; }

void GetOneInstanceRequest::SetTimeout(uint64_t timeout) { impl->timeout_ = timeout; }

void GetOneInstanceRequest::SetLabels(const std::map<std::string, std::string>& labels) {
  if (impl->labels_.IsNull()) {
    impl->labels_.Set(new std::map<std::string, std::string>(labels));
  } else {
    *impl->labels_ = labels;
  }
}

void GetOneInstanceRequest::SetMetadata(std::map<std::string, std::string>& metadata) {
  if (impl->metadata_param_.IsNull()) {
    impl->metadata_param_.Set(new MetadataRouterParam());
  }
  impl->metadata_param_->metadata_ = metadata;
}

void GetOneInstanceRequest::SetMetadataFailover(MetadataFailoverType metadata_failover_type) {
  if (impl->metadata_param_.IsNull()) {
    impl->metadata_param_.Set(new MetadataRouterParam());
  }
  impl->metadata_param_->failover_type_ = metadata_failover_type;
}

void GetOneInstanceRequest::SetLoadBalanceType(LoadBalanceType load_balance_type) {
  impl->load_balance_type_ = load_balance_type;
}

void GetOneInstanceRequest::SetReplicateIndex(int replicate_index) {
  impl->criteria_.replicate_index_ = replicate_index;
}

// 单个服务请求读取
const ServiceKey& GetOneInstanceRequestAccessor::GetServiceKey() {
  return request_.impl->service_key_;
}

const Criteria& GetOneInstanceRequestAccessor::GetCriteria() { return request_.impl->criteria_; }

const std::map<std::string, std::string>& GetOneInstanceRequestAccessor::GetLabels() const {
  return request_.impl->labels_.NotNull() ? *request_.impl->labels_ : EmptyStringMap();
}

MetadataRouterParam* GetOneInstanceRequestAccessor::GetMetadataParam() const {
  return request_.impl->metadata_param_.Get();
}

LoadBalanceType GetOneInstanceRequestAccessor::GetLoadBalanceType() {
  return request_.impl->load_balance_type_;
}

bool GetOneInstanceRequestAccessor::HasSourceService() {
  return request_.impl->source_service_.NotNull();
}
ServiceInfo* GetOneInstanceRequestAccessor::GetSourceService() {
  return request_.impl->source_service_.Get();
}

ServiceInfo* GetOneInstanceRequestAccessor::DumpSourceService() {
  return request_.impl->source_service_.NotNull() ? new ServiceInfo(*request_.impl->source_service_)
                                                  : NULL;
}

bool GetOneInstanceRequestAccessor::HasFlowId() { return request_.impl->flow_id_.HasValue(); }
uint64_t GetOneInstanceRequestAccessor::GetFlowId() { return request_.impl->flow_id_.Value(); }
void GetOneInstanceRequestAccessor::SetFlowId(uint64_t flow_id) {
  request_.impl->flow_id_ = flow_id;
}

bool GetOneInstanceRequestAccessor::HasTimeout() { return request_.impl->timeout_.HasValue(); }
uint64_t GetOneInstanceRequestAccessor::GetTimeout() { return request_.impl->timeout_.Value(); }
void GetOneInstanceRequestAccessor::SetTimeout(uint64_t timeout) {
  request_.impl->timeout_ = timeout;
}

GetOneInstanceRequest* GetOneInstanceRequestAccessor::Dump() {
  GetOneInstanceRequest* dump = new GetOneInstanceRequest(request_.impl->service_key_);
  dump->impl->criteria_       = request_.impl->criteria_;
  if (HasSourceService()) dump->SetSourceService(*GetSourceService());
  if (HasFlowId()) dump->SetFlowId(GetFlowId());
  if (HasTimeout()) dump->SetTimeout(GetTimeout());
  dump->impl->load_balance_type_ = request_.impl->load_balance_type_;
  if (request_.impl->labels_.NotNull()) dump->SetLabels(GetLabels());
  if (request_.impl->metadata_param_.NotNull()) {
    dump->impl->metadata_param_.Set(new MetadataRouterParam(*request_.impl->metadata_param_));
  }
  return dump;
}

// 批量获取服务实例请求
GetInstancesRequest::GetInstancesRequest(const ServiceKey& service_key) {
  impl                = new GetInstancesRequestImpl();
  impl->service_key_  = service_key;
  impl->request_flag_ = 0;
}

GetInstancesRequest::~GetInstancesRequest() { delete impl; }

void GetInstancesRequest::SetSourceService(const ServiceInfo& source_service) {
  if (impl->source_service_.IsNull()) {
    impl->source_service_.Set(new ServiceInfo(source_service));
  } else {
    *impl->source_service_ = source_service;
  }
}

bool GetInstancesRequest::SetSourceSetName(const std::string& set_name) {
  if (impl->source_service_.IsNull()) {
    impl->source_service_.Set(new ServiceInfo());
  }
  impl->source_service_->metadata_[constants::kRouterRequestSetNameKey] = set_name;
  return true;
}

void GetInstancesRequest::SetCanary(const std::string& canary) {
  if (impl->source_service_.IsNull()) {
    impl->source_service_.Set(new ServiceInfo());
  }
  impl->source_service_->metadata_[constants::kRouterRequestCanaryKey] = canary;
}

void GetInstancesRequest::SetIncludeCircuitBreakInstances(bool include_circuit_breaker_instances) {
  if (include_circuit_breaker_instances) {
    impl->request_flag_ = impl->request_flag_ | kGetInstancesRequestIncludeCircuitBreaker;
  } else {
    impl->request_flag_ = impl->request_flag_ & ~kGetInstancesRequestIncludeCircuitBreaker;
  }
}

void GetInstancesRequest::SetIncludeUnhealthyInstances(bool include_unhealthy_instances) {
  if (include_unhealthy_instances) {
    impl->request_flag_ = impl->request_flag_ | kGetInstancesRequestIncludeUnhealthy;
  } else {
    impl->request_flag_ = impl->request_flag_ & ~kGetInstancesRequestIncludeUnhealthy;
  }
}

void GetInstancesRequest::SetSkipRouteFilter(bool skip_route_filter) {
  if (skip_route_filter) {
    impl->request_flag_ = impl->request_flag_ | kGetInstancesRequestSkipRouter;
  } else {
    impl->request_flag_ = impl->request_flag_ & ~kGetInstancesRequestSkipRouter;
  }
}

void GetInstancesRequest::SetMetadata(std::map<std::string, std::string>& metadata) {
  if (impl->metadata_param_.IsNull()) {
    impl->metadata_param_.Set(new MetadataRouterParam());
  }
  impl->metadata_param_->metadata_ = metadata;
}

void GetInstancesRequest::SetMetadataFailover(MetadataFailoverType metadata_failover_type) {
  if (impl->metadata_param_.IsNull()) {
    impl->metadata_param_.Set(new MetadataRouterParam());
  }
  impl->metadata_param_->failover_type_ = metadata_failover_type;
}

void GetInstancesRequest::SetFlowId(uint64_t flow_id) { impl->flow_id_ = flow_id; }

void GetInstancesRequest::SetTimeout(uint64_t timeout) { impl->timeout_ = timeout; }

// 批量获取服务实例请求读取
const ServiceKey& GetInstancesRequestAccessor::GetServiceKey() {
  return request_.impl->service_key_;
}

bool GetInstancesRequestAccessor::HasSourceService() {
  return request_.impl->source_service_.NotNull();
}
ServiceInfo* GetInstancesRequestAccessor::GetSourceService() {
  return request_.impl->source_service_.Get();
}

ServiceInfo* GetInstancesRequestAccessor::DumpSourceService() {
  return request_.impl->source_service_.NotNull() ? new ServiceInfo(*request_.impl->source_service_)
                                                  : NULL;
}

bool GetInstancesRequestAccessor::GetIncludeCircuitBreakerInstances() {
  return request_.impl->request_flag_ & kGetInstancesRequestIncludeCircuitBreaker;
}

bool GetInstancesRequestAccessor::GetIncludeUnhealthyInstances() {
  return request_.impl->request_flag_ & kGetInstancesRequestIncludeUnhealthy;
}

bool GetInstancesRequestAccessor::GetSkipRouteFilter() {
  return request_.impl->request_flag_ & kGetInstancesRequestSkipRouter;
}

MetadataRouterParam* GetInstancesRequestAccessor::GetMetadataParam() const {
  return request_.impl->metadata_param_.Get();
}

bool GetInstancesRequestAccessor::HasFlowId() { return request_.impl->flow_id_.HasValue(); }
uint64_t GetInstancesRequestAccessor::GetFlowId() { return request_.impl->flow_id_.Value(); }
void GetInstancesRequestAccessor::SetFlowId(uint64_t flow_id) { request_.impl->flow_id_ = flow_id; }

bool GetInstancesRequestAccessor::HasTimeout() { return request_.impl->timeout_.HasValue(); }
uint64_t GetInstancesRequestAccessor::GetTimeout() { return request_.impl->timeout_.Value(); }
void GetInstancesRequestAccessor::SetTimeout(uint64_t timeout) {
  request_.impl->timeout_ = timeout;
}

GetInstancesRequest* GetInstancesRequestAccessor::Dump() {
  GetInstancesRequest* dump = new GetInstancesRequest(request_.impl->service_key_);
  if (HasSourceService()) dump->SetSourceService(*GetSourceService());
  dump->impl->request_flag_ = request_.impl->request_flag_;
  if (HasFlowId()) dump->SetFlowId(GetFlowId());
  if (HasTimeout()) dump->SetTimeout(GetTimeout());
  if (request_.impl->metadata_param_.NotNull()) {
    dump->impl->metadata_param_.Set(new MetadataRouterParam(*request_.impl->metadata_param_));
  }
  return dump;
}

// 调用上报请求
ServiceCallResult::ServiceCallResult() { impl = new ServiceCallResultImpl(); }

ServiceCallResult::~ServiceCallResult() { delete impl; }

void ServiceCallResult::SetServiceName(const std::string& service_name) {
  impl->service_name_ = service_name;
}

void ServiceCallResult::SetServiceNamespace(const std::string& service_namespace) {
  impl->service_namespace_ = service_namespace;
}

void ServiceCallResult::SetInstanceId(const std::string& instance_id) {
  impl->instance_id_ = instance_id;
}

void ServiceCallResult::SetInstanceHostAndPort(const std::string& host, int port) {
  impl->host_ = host;
  impl->port_ = port;
}

void ServiceCallResult::SetRetStatus(CallRetStatus ret_status) { impl->ret_status_ = ret_status; }

void ServiceCallResult::SetRetCode(int ret_code) { impl->ret_code_ = ret_code; }

void ServiceCallResult::SetDelay(uint64_t delay) { impl->delay_ = delay; }

void ServiceCallResult::SetSource(ServiceKey& service_key) { impl->source_ = service_key; }

void ServiceCallResult::SetSubset(const std::map<std::string, std::string>& subset) {
  impl->subset_ = subset;
}

void ServiceCallResult::SetLabels(const std::map<std::string, std::string>& labels) {
  impl->labels_ = labels;
}

// 调用上报读取
const std::string& ServiceCallResultGetter::GetServiceName() { return result_.impl->service_name_; }

const std::string& ServiceCallResultGetter::GetServiceNamespace() {
  return result_.impl->service_namespace_;
}

const std::string& ServiceCallResultGetter::GetInstanceId() { return result_.impl->instance_id_; }

const std::string& ServiceCallResultGetter::GetHost() { return result_.impl->host_; }

int ServiceCallResultGetter::GetPort() { return result_.impl->port_; }

CallRetStatus ServiceCallResultGetter::GetRetStatus() { return result_.impl->ret_status_; }

int ServiceCallResultGetter::GetRetCode() { return result_.impl->ret_code_; }

uint64_t ServiceCallResultGetter::GetDelay() { return result_.impl->delay_; }

const ServiceKey& ServiceCallResultGetter::GetSource() { return result_.impl->source_; }

const std::map<std::string, std::string>& ServiceCallResultGetter::GetSubset() {
  return result_.impl->subset_;
}

const std::map<std::string, std::string>& ServiceCallResultGetter::GetLabels() {
  return result_.impl->labels_;
}

/// @brief 创建服务注册请求
InstanceRegisterRequest::InstanceRegisterRequest(const std::string& service_namespace,
                                                 const std::string& service_name,
                                                 const std::string& service_token,
                                                 const std::string& host, int port) {
  impl                     = new InstanceRegisterRequestImpl();
  impl->service_namespace_ = service_namespace;
  impl->service_name_      = service_name;
  impl->service_token_     = service_token;
  impl->host_              = host;
  impl->port_              = port;
}

InstanceRegisterRequest::~InstanceRegisterRequest() { delete impl; }

void InstanceRegisterRequest::SetFlowId(uint64_t flow_id) { impl->flow_id_ = flow_id; }

void InstanceRegisterRequest::SetTimeout(uint64_t timeout) { impl->timeout_ = timeout; }

void InstanceRegisterRequest::SetVpcId(const std::string& vpc_id) { impl->vpc_id_ = vpc_id; }

void InstanceRegisterRequest::SetProtocol(const std::string& protocol) {
  impl->protocol_ = protocol;
}

void InstanceRegisterRequest::SetWeight(int weight) { impl->weight_ = weight; }

void InstanceRegisterRequest::SetPriority(int priority) { impl->priority_ = priority; }

void InstanceRegisterRequest::SetVersion(const std::string& version) { impl->version_ = version; }

void InstanceRegisterRequest::SetMetadata(const std::map<std::string, std::string>& metadata) {
  impl->metadata_ = metadata;
}

void InstanceRegisterRequest::SetHealthCheckFlag(bool health_check_flag) {
  impl->health_check_flag_ = health_check_flag;
}

void InstanceRegisterRequest::SetHealthCheckType(HealthCheckType health_check_type) {
  impl->health_check_type_ = health_check_type;
}

void InstanceRegisterRequest::SetTtl(int ttl) { impl->ttl_ = ttl; }

// 注册请求读取
const std::string& InstanceRegisterRequestAccessor::GetServiceNamespace() {
  return request_.impl->service_namespace_;
}

const std::string& InstanceRegisterRequestAccessor::GetServiceName() {
  return request_.impl->service_name_;
}

const std::string& InstanceRegisterRequestAccessor::GetServiceToken() {
  return request_.impl->service_token_;
}

const std::string& InstanceRegisterRequestAccessor::GetHost() { return request_.impl->host_; }

int InstanceRegisterRequestAccessor::GetPort() { return request_.impl->port_; }

bool InstanceRegisterRequestAccessor::HasFlowId() { return request_.impl->flow_id_.HasValue(); }
uint64_t InstanceRegisterRequestAccessor::GetFlowId() { return request_.impl->flow_id_.Value(); }

bool InstanceRegisterRequestAccessor::HasTimeout() { return request_.impl->timeout_.HasValue(); }
uint64_t InstanceRegisterRequestAccessor::GetTimeout() { return request_.impl->timeout_.Value(); }

bool InstanceRegisterRequestAccessor::HasVpcId() { return request_.impl->vpc_id_.HasValue(); }
const std::string& InstanceRegisterRequestAccessor::GetVpcId() {
  return request_.impl->vpc_id_.Value();
}

bool InstanceRegisterRequestAccessor::HasProtocol() { return request_.impl->protocol_.HasValue(); }
const std::string& InstanceRegisterRequestAccessor::GetProtocol() {
  return request_.impl->protocol_.Value();
}

bool InstanceRegisterRequestAccessor::HasWeight() { return request_.impl->weight_.HasValue(); }
int InstanceRegisterRequestAccessor::GetWeight() { return request_.impl->weight_.Value(); }

bool InstanceRegisterRequestAccessor::HasPriority() { return request_.impl->protocol_.HasValue(); }
int InstanceRegisterRequestAccessor::GetPriority() { return request_.impl->priority_.Value(); }

bool InstanceRegisterRequestAccessor::HasVersion() { return request_.impl->version_.HasValue(); }
const std::string& InstanceRegisterRequestAccessor::GetVersion() {
  return request_.impl->version_.Value();
}

bool InstanceRegisterRequestAccessor::HasMetadata() { return request_.impl->metadata_.HasValue(); }
std::map<std::string, std::string>& InstanceRegisterRequestAccessor::GetMetadata() {
  return request_.impl->metadata_.Value();
}

bool InstanceRegisterRequestAccessor::HasHealthCheckFlag() {
  return request_.impl->health_check_flag_.HasValue();
}
bool InstanceRegisterRequestAccessor::GetHealthCheckFlag() {
  return request_.impl->health_check_flag_.Value();
}

bool InstanceRegisterRequestAccessor::HasHealthCheckType() {
  return request_.impl->health_check_type_.HasValue();
}
HealthCheckType InstanceRegisterRequestAccessor::GetHealthCheckType() {
  return request_.impl->health_check_type_.Value();
}

bool InstanceRegisterRequestAccessor::HasTtl() { return request_.impl->ttl_.HasValue(); }
int InstanceRegisterRequestAccessor::GetTtl() { return request_.impl->ttl_.Value(); }

// 反注册请求
InstanceDeregisterRequest::InstanceDeregisterRequest(const std::string& service_token,
                                                     const std::string& instance_id) {
  impl                 = new InstanceDeregisterRequestImpl();
  impl->service_token_ = service_token;
  impl->instance_id_   = instance_id;
}

InstanceDeregisterRequest::InstanceDeregisterRequest(const std::string& service_namespace,
                                                     const std::string& service_name,
                                                     const std::string& service_token,
                                                     const std::string& host, int port) {
  impl                     = new InstanceDeregisterRequestImpl();
  impl->service_namespace_ = service_namespace;
  impl->service_name_      = service_name;
  impl->service_token_     = service_token;
  impl->host_              = host;
  impl->port_              = port;
}

InstanceDeregisterRequest::~InstanceDeregisterRequest() { delete impl; }

void InstanceDeregisterRequest::SetVpcId(const std::string& vpc_id) { impl->vpc_id_ = vpc_id; }

void InstanceDeregisterRequest::SetFlowId(uint64_t flow_id) { impl->flow_id_ = flow_id; }

void InstanceDeregisterRequest::SetTimeout(uint64_t timeout) { impl->timeout_ = timeout; }

// 反注册请求读取
const std::string& InstanceDeregisterRequestAccessor::GetServiceNamespace() {
  return request_.impl->service_namespace_;
}

const std::string& InstanceDeregisterRequestAccessor::GetServiceName() {
  return request_.impl->service_name_;
}

const std::string& InstanceDeregisterRequestAccessor::GetServiceToken() {
  return request_.impl->service_token_;
}

bool InstanceDeregisterRequestAccessor::HasInstanceId() {
  return request_.impl->instance_id_.HasValue();
}
const std::string& InstanceDeregisterRequestAccessor::GetInstanceId() {
  return request_.impl->instance_id_.Value();
}

bool InstanceDeregisterRequestAccessor::HasVpcId() { return request_.impl->vpc_id_.HasValue(); }
const std::string& InstanceDeregisterRequestAccessor::GetVpcId() {
  return request_.impl->vpc_id_.Value();
}

const std::string& InstanceDeregisterRequestAccessor::GetHost() { return request_.impl->host_; }

int InstanceDeregisterRequestAccessor::GetPort() { return request_.impl->port_; }

bool InstanceDeregisterRequestAccessor::HasFlowId() { return request_.impl->flow_id_.HasValue(); }
uint64_t InstanceDeregisterRequestAccessor::GetFlowId() { return request_.impl->flow_id_.Value(); }

bool InstanceDeregisterRequestAccessor::HasTimeout() { return request_.impl->timeout_.HasValue(); }
uint64_t InstanceDeregisterRequestAccessor::GetTimeout() { return request_.impl->timeout_.Value(); }

// 心跳请求
InstanceHeartbeatRequest::InstanceHeartbeatRequest(const std::string& service_token,
                                                   const std::string& instance_id) {
  impl                 = new InstanceHeartbeatRequestImpl();
  impl->service_token_ = service_token;
  impl->instance_id_   = instance_id;
}

InstanceHeartbeatRequest::InstanceHeartbeatRequest(const std::string& service_namespace,
                                                   const std::string& service_name,
                                                   const std::string& service_token,
                                                   const std::string& host, int port) {
  impl                     = new InstanceHeartbeatRequestImpl();
  impl->service_namespace_ = service_namespace;
  impl->service_name_      = service_name;
  impl->service_token_     = service_token;
  impl->host_              = host;
  impl->port_              = port;
}

InstanceHeartbeatRequest::~InstanceHeartbeatRequest() { delete impl; }

void InstanceHeartbeatRequest::SetVpcId(const std::string& vpc_id) { impl->vpc_id_ = vpc_id; }

void InstanceHeartbeatRequest::SetFlowId(uint64_t flow_id) { impl->flow_id_ = flow_id; }

void InstanceHeartbeatRequest::SetTimeout(uint64_t timeout) { impl->timeout_ = timeout; }

// 心跳请求读取
const std::string& InstanceHeartbeatRequestAccessor::GetServiceNamespace() {
  return request_.impl->service_namespace_;
}

const std::string& InstanceHeartbeatRequestAccessor::GetServiceName() {
  return request_.impl->service_name_;
}

const std::string& InstanceHeartbeatRequestAccessor::GetServiceToken() {
  return request_.impl->service_token_;
}

bool InstanceHeartbeatRequestAccessor::HasInstanceId() {
  return request_.impl->instance_id_.HasValue();
}
const std::string& InstanceHeartbeatRequestAccessor::GetInstanceId() {
  return request_.impl->instance_id_.Value();
}

const std::string& InstanceHeartbeatRequestAccessor::GetHost() { return request_.impl->host_; }

bool InstanceHeartbeatRequestAccessor::HasVpcId() { return request_.impl->vpc_id_.HasValue(); }
const std::string& InstanceHeartbeatRequestAccessor::GetVpcId() {
  return request_.impl->vpc_id_.Value();
}

int InstanceHeartbeatRequestAccessor::GetPort() { return request_.impl->port_; }

bool InstanceHeartbeatRequestAccessor::HasFlowId() { return request_.impl->flow_id_.HasValue(); }
uint64_t InstanceHeartbeatRequestAccessor::GetFlowId() { return request_.impl->flow_id_.Value(); }

bool InstanceHeartbeatRequestAccessor::HasTimeout() { return request_.impl->timeout_.HasValue(); }
uint64_t InstanceHeartbeatRequestAccessor::GetTimeout() { return request_.impl->timeout_.Value(); }

}  // namespace polaris
