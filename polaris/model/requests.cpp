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

namespace polaris {

// 获取单个服务请求
GetOneInstanceRequest::GetOneInstanceRequest(const ServiceKey& service_key)
    : impl_(new GetOneInstanceRequest::Impl(service_key)) {}

GetOneInstanceRequest::~GetOneInstanceRequest() {
  delete impl_;
  impl_ = nullptr;
}

void GetOneInstanceRequest::SetHashKey(uint64_t hash_key) { impl_->criteria_.hash_key_ = hash_key; }

void GetOneInstanceRequest::SetHashString(const std::string& hash_string) {
  impl_->criteria_.hash_string_ = hash_string;
}

void GetOneInstanceRequest::SetIgnoreHalfOpen(bool ignore_half_open) {
  impl_->criteria_.ignore_half_open_ = ignore_half_open;
}

void GetOneInstanceRequest::SetSourceService(const ServiceInfo& source_service) {
  if (impl_->source_service_ == nullptr) {
    impl_->source_service_.reset(new ServiceInfo(source_service));
  } else {
    *impl_->source_service_ = source_service;
  }
}

bool GetOneInstanceRequest::SetSourceSetName(const std::string& set_name) {
  if (impl_->source_service_ == nullptr) {
    impl_->source_service_.reset(new ServiceInfo());
  }
  impl_->source_service_->metadata_[constants::kRouterRequestSetNameKey] = set_name;
  return true;
}

void GetOneInstanceRequest::SetCanary(const std::string& canary) {
  if (impl_->source_service_ == nullptr) {
    impl_->source_service_.reset(new ServiceInfo());
  }
  impl_->source_service_->metadata_[constants::kRouterRequestCanaryKey] = canary;
}

void GetOneInstanceRequest::SetFlowId(uint64_t flow_id) { impl_->flow_id_ = flow_id; }

void GetOneInstanceRequest::SetTimeout(uint64_t timeout) { impl_->timeout_ = timeout; }

void GetOneInstanceRequest::SetLabels(const std::map<std::string, std::string>& labels) {
  if (impl_->labels_ == nullptr) {
    impl_->labels_.reset(new std::map<std::string, std::string>(labels));
  } else {
    *impl_->labels_ = labels;
  }
}

void GetOneInstanceRequest::SetMetadata(std::map<std::string, std::string>& metadata) {
  if (impl_->metadata_param_ == nullptr) {
    impl_->metadata_param_.reset(new MetadataRouterParam());
  }
  impl_->metadata_param_->metadata_ = metadata;
}

void GetOneInstanceRequest::SetMetadataFailover(MetadataFailoverType metadata_failover_type) {
  if (impl_->metadata_param_ == nullptr) {
    impl_->metadata_param_.reset(new MetadataRouterParam());
  }
  impl_->metadata_param_->failover_type_ = metadata_failover_type;
}

void GetOneInstanceRequest::SetLoadBalanceType(LoadBalanceType load_balance_type) {
  impl_->load_balance_type_ = load_balance_type;
}

void GetOneInstanceRequest::SetBackupInstanceNum(uint32_t backup_instance_num) {
  impl_->backup_instance_num_ = backup_instance_num;
}

void GetOneInstanceRequest::SetReplicateIndex(int replicate_index) {
  impl_->criteria_.replicate_index_ = replicate_index;
}

GetOneInstanceRequest::Impl& GetOneInstanceRequest::GetImpl() const { return *impl_; }

GetOneInstanceRequest::Impl* GetOneInstanceRequest::Impl::Dump() const {
  GetOneInstanceRequest::Impl* dump = new GetOneInstanceRequest::Impl(service_key_);
  dump->criteria_ = criteria_;
  if (source_service_ != nullptr) {
    dump->source_service_.reset(new ServiceInfo(*source_service_));
  }
  if (flow_id_.HasValue()) dump->flow_id_ = flow_id_;
  if (timeout_.HasValue()) dump->timeout_ = timeout_;
  dump->load_balance_type_ = load_balance_type_;
  if (labels_ != nullptr) dump->labels_.reset(new std::map<std::string, std::string>(GetLabels()));
  if (metadata_param_ != nullptr) {
    dump->metadata_param_.reset(new MetadataRouterParam(*metadata_param_));
  }
  return dump;
}

// 批量获取服务实例请求
GetInstancesRequest::GetInstancesRequest(const ServiceKey& service_key)
    : impl_(new GetInstancesRequest::Impl(service_key)) {}

GetInstancesRequest::~GetInstancesRequest() {
  delete impl_;
  impl_ = nullptr;
}

void GetInstancesRequest::SetSourceService(const ServiceInfo& source_service) {
  if (impl_->source_service_ == nullptr) {
    impl_->source_service_.reset(new ServiceInfo(source_service));
  } else {
    *impl_->source_service_ = source_service;
  }
}

bool GetInstancesRequest::SetSourceSetName(const std::string& set_name) {
  if (impl_->source_service_ == nullptr) {
    impl_->source_service_.reset(new ServiceInfo());
  }
  impl_->source_service_->metadata_[constants::kRouterRequestSetNameKey] = set_name;
  return true;
}

void GetInstancesRequest::SetCanary(const std::string& canary) {
  if (impl_->source_service_ == nullptr) {
    impl_->source_service_.reset(new ServiceInfo());
  }
  impl_->source_service_->metadata_[constants::kRouterRequestCanaryKey] = canary;
}

void GetInstancesRequest::SetIncludeCircuitBreakInstances(bool include_circuit_breaker_instances) {
  if (include_circuit_breaker_instances) {
    impl_->request_flag_ = impl_->request_flag_ | kGetInstancesRequestIncludeCircuitBreaker;
  } else {
    impl_->request_flag_ = impl_->request_flag_ & ~kGetInstancesRequestIncludeCircuitBreaker;
  }
}

void GetInstancesRequest::SetIncludeUnhealthyInstances(bool include_unhealthy_instances) {
  if (include_unhealthy_instances) {
    impl_->request_flag_ = impl_->request_flag_ | kGetInstancesRequestIncludeUnhealthy;
  } else {
    impl_->request_flag_ = impl_->request_flag_ & ~kGetInstancesRequestIncludeUnhealthy;
  }
}

void GetInstancesRequest::SetSkipRouteFilter(bool skip_route_filter) {
  if (skip_route_filter) {
    impl_->request_flag_ = impl_->request_flag_ | kGetInstancesRequestSkipRouter;
  } else {
    impl_->request_flag_ = impl_->request_flag_ & ~kGetInstancesRequestSkipRouter;
  }
}

void GetInstancesRequest::SetMetadata(std::map<std::string, std::string>& metadata) {
  if (impl_->metadata_param_ == nullptr) {
    impl_->metadata_param_.reset(new MetadataRouterParam());
  }
  impl_->metadata_param_->metadata_ = metadata;
}

void GetInstancesRequest::SetMetadataFailover(MetadataFailoverType metadata_failover_type) {
  if (impl_->metadata_param_ == nullptr) {
    impl_->metadata_param_.reset(new MetadataRouterParam());
  }
  impl_->metadata_param_->failover_type_ = metadata_failover_type;
}

void GetInstancesRequest::SetFlowId(uint64_t flow_id) { impl_->flow_id_ = flow_id; }

void GetInstancesRequest::SetTimeout(uint64_t timeout) { impl_->timeout_ = timeout; }

GetInstancesRequest::Impl& GetInstancesRequest::GetImpl() const { return *impl_; }

GetInstancesRequest::Impl* GetInstancesRequest::Impl::Dump() const {
  GetInstancesRequest::Impl* dump = new GetInstancesRequest::Impl(service_key_);
  if (source_service_ != nullptr) {
    dump->source_service_.reset(new ServiceInfo(*source_service_));
  }
  dump->request_flag_ = request_flag_;
  if (flow_id_.HasValue()) dump->flow_id_ = flow_id_;
  if (timeout_.HasValue()) dump->timeout_ = timeout_;
  if (metadata_param_ != nullptr) {
    dump->metadata_param_.reset(new MetadataRouterParam(*metadata_param_));
  }
  return dump;
}

// 调用上报请求
ServiceCallResult::ServiceCallResult() : impl_(new ServiceCallResult::Impl()) {}

ServiceCallResult::~ServiceCallResult() {
  delete impl_;
  impl_ = nullptr;
}

void ServiceCallResult::SetServiceName(const std::string& service_name) {
  impl_->gauge_.service_key_.name_ = service_name;
}

void ServiceCallResult::SetServiceNamespace(const std::string& service_namespace) {
  impl_->gauge_.service_key_.namespace_ = service_namespace;
}

void ServiceCallResult::SetInstanceId(const std::string& instance_id) { impl_->gauge_.instance_id = instance_id; }

void ServiceCallResult::SetInstanceHostAndPort(const std::string& host, int port) {
  if (impl_->instance_host_port_ == nullptr) {
    impl_->instance_host_port_ = new InstanceHostPortKey();
  }
  impl_->instance_host_port_->host_ = host;
  impl_->instance_host_port_->port_ = port;
}

void ServiceCallResult::SetRetStatus(CallRetStatus ret_status) { impl_->gauge_.call_ret_status = ret_status; }

void ServiceCallResult::SetRetCode(int ret_code) { impl_->gauge_.call_ret_code = ret_code; }

void ServiceCallResult::SetDelay(uint64_t delay) { impl_->gauge_.call_daley = delay; }

void ServiceCallResult::SetSource(const ServiceKey& service_key) {
  if (impl_->gauge_.source_service_key == nullptr) {
    impl_->gauge_.source_service_key = new ServiceKey(service_key);
  } else {
    *impl_->gauge_.source_service_key = service_key;
  }
}

void ServiceCallResult::SetSubset(const std::map<std::string, std::string>& subset) {
  if (impl_->gauge_.subset_ == nullptr) {
    impl_->gauge_.subset_ = new std::map<std::string, std::string>(subset);
  } else {
    *impl_->gauge_.subset_ = subset;
  }
}

void ServiceCallResult::SetLabels(const std::map<std::string, std::string>& labels) {
  if (impl_->gauge_.labels_ == nullptr) {
    impl_->gauge_.labels_ = new std::map<std::string, std::string>(labels);
  } else {
    *impl_->gauge_.labels_ = labels;
  }
}

void ServiceCallResult::SetLocalityAwareInfo(uint64_t locality_aware_info) {
  impl_->gauge_.locality_aware_info = locality_aware_info;
}

ServiceCallResult::Impl& ServiceCallResult::GetImpl() const { return *impl_; }

}  // namespace polaris
