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

#include "model/instance.h"

#include "model/constants.h"
#include "v1/service.pb.h"

namespace polaris {

// 默认实例对象
static std::shared_ptr<InstanceImpl>& GetDefaultInstance() {
  static std::shared_ptr<InstanceImpl> instance(new InstanceImpl());
  return instance;
}

Instance::Instance() : impl_(GetDefaultInstance()) {}

Instance::Instance(const std::string& id, const std::string& host, const int& port, const uint32_t& weight)
    : impl_(new InstanceImpl(id, host, port, weight)) {}

Instance::Instance(const Instance& other) : impl_(other.impl_) {}

const Instance& Instance::operator=(const Instance& other) {
  impl_ = other.impl_;
  return *this;
}

Instance::~Instance() {}

const std::string& Instance::GetHost() const { return impl_->remote_value_->host_; }

int Instance::GetPort() const { return impl_->remote_value_->port_; }

bool Instance::IsIpv6() const { return impl_->remote_value_->is_ipv6_; }

const std::string& Instance::GetVpcId() const { return impl_->remote_value_->vpc_id_; }

const std::string& Instance::GetId() const { return impl_->remote_value_->id_; }

const std::string& Instance::GetProtocol() const { return impl_->remote_value_->protocol_; }

const std::string& Instance::GetVersion() const { return impl_->remote_value_->version_; }

uint32_t Instance::GetWeight() const { return impl_->remote_value_->weight_; }

int Instance::GetPriority() const { return impl_->remote_value_->priority_; }

bool Instance::isHealthy() const { return impl_->remote_value_->is_healthy_; }

bool Instance::isIsolate() const { return impl_->remote_value_->is_isolate_; }

const std::map<std::string, std::string>& Instance::GetMetadata() const { return impl_->remote_value_->metadata_; }

const std::string& Instance::GetContainerName() const { return impl_->remote_value_->container_name_; }

const std::string& Instance::GetInternalSetName() const { return impl_->remote_value_->internal_set_name_; }

const std::string& Instance::GetLogicSet() const { return impl_->remote_value_->logic_set_; }

const std::string& Instance::GetRegion() const { return impl_->remote_value_->region_; }

const std::string& Instance::GetZone() const { return impl_->remote_value_->zone_; }

const std::string& Instance::GetCampus() const { return impl_->remote_value_->campus_; }

uint32_t Instance::GetDynamicWeight() const { return impl_->local_value_->dynamic_weight_; }

uint64_t Instance::GetLocalId() const { return impl_->local_value_->local_id_; }

uint64_t Instance::GetHash() const { return impl_->local_value_->hash_; }

uint64_t Instance::GetLocalityAwareInfo() const { return impl_->owned_value_.locality_aware_info_; }

InstanceImpl& Instance::GetImpl() {
  if (impl_ == GetDefaultInstance()) {  // 初始化的默认值，需要重新初始化
    impl_.reset(new InstanceImpl());
  }
  return *impl_;
}

InstanceRemoteValue::InstanceRemoteValue()
    : port_(0), weight_(0), priority_(0), is_ipv6_(false), is_healthy_(false), is_isolate_(false) {}

InstanceRemoteValue::InstanceRemoteValue(const std::string& id, const std::string& host, const int& port,
                                         const uint32_t& weight)
    : id_(id), host_(host), port_(port), weight_(weight), priority_(0), is_healthy_(true), is_isolate_(false) {
  is_ipv6_ = host.find(':') != std::string::npos;
}

void InstanceRemoteValue::InitFromPb(const v1::Instance& instance) {
  id_ = instance.id().value();
  host_ = instance.host().value();
  port_ = instance.port().value();
  is_ipv6_ = host_.find(':') != std::string::npos;
  weight_ = instance.weight().value();
  vpc_id_ = instance.vpc_id().value();
  protocol_ = instance.protocol().value();
  version_ = instance.version().value();
  priority_ = instance.priority().value();
  if (instance.has_healthy()) {
    is_healthy_ = instance.healthy().value();
  }
  if (instance.has_isolate()) {
    is_isolate_ = instance.isolate().value();
  }
  for (auto metadata_it = instance.metadata().begin(); metadata_it != instance.metadata().end(); metadata_it++) {
    metadata_[metadata_it->first] = metadata_it->second;
    // 解析container_name和internal-set-name
    if (!metadata_it->first.compare(constants::kContainerNameKey)) {
      container_name_ = metadata_it->second;
    }
    if (!metadata_it->first.compare(constants::kRouterRequestSetNameKey)) {
      internal_set_name_ = metadata_it->second;
    }
  }
  logic_set_ = instance.logic_set().value();
  if (instance.has_location()) {
    region_ = instance.location().region().value();
    zone_ = instance.location().zone().value();
    campus_ = instance.location().campus().value();
  }
}

InstanceImpl::InstanceImpl() : remote_value_(new InstanceRemoteValue()), local_value_(new InstanceLocalValue()) {}

InstanceImpl::InstanceImpl(const std::string& id, const std::string& host, const int& port, const uint32_t& weight)
    : remote_value_(new InstanceRemoteValue(id, host, port, weight)), local_value_(new InstanceLocalValue()) {}

void InstanceImpl::InitFromPb(const v1::Instance& instance) {
  remote_value_->InitFromPb(instance);
  // 设置动态权重大小默认为静态权重
  local_value_->dynamic_weight_ = remote_value_->weight_;
}

void InstanceImpl::SetDynamicWeight(uint32_t dynamic_weight) { local_value_->dynamic_weight_ = dynamic_weight; }

void InstanceImpl::SetHashValue(uint64_t hashVal) { local_value_->hash_ = hashVal; }

void InstanceImpl::SetLocalId(uint64_t local_id) { local_value_->local_id_ = local_id; }

void InstanceImpl::CopyLocalValue(const InstanceImpl& impl) { local_value_ = impl.local_value_; }

Instance* InstanceImpl::DumpWithLocalityAwareInfo(uint64_t locality_aware_info) {
  Instance* new_instance = new Instance();
  auto& instance_impl = new_instance->GetImpl();
  instance_impl.remote_value_ = this->remote_value_;
  instance_impl.local_value_ = this->local_value_;
  instance_impl.owned_value_.locality_aware_info_ = locality_aware_info;
  return new_instance;
}

}  // namespace polaris
