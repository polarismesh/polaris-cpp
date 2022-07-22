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

#include "plugin/service_router/route_info.h"

#include "logger.h"
#include "model/constants.h"
#include "model/requests.h"

namespace polaris {

RouteInfo::RouteInfo(const ServiceKey& service_key, ServiceInfo* source_service_info)
    : service_key_(service_key),
      source_service_info_(source_service_info),
      source_service_data_(nullptr),
      service_instances_(nullptr),
      service_route_rule_(nullptr),
      source_service_route_rule_(nullptr),
      request_flag_(0),
      nearby_disable_(false),
      labels_(nullptr),
      metadata_param_(nullptr),
      circuit_breaker_version_(0) {}

RouteInfo::RouteInfo(const ServiceKey& service_key, ServiceInfo* source_service_info, ServiceData* source_service_data)
    : service_key_(service_key),
      source_service_info_(source_service_info),
      source_service_data_(source_service_data),
      service_instances_(nullptr),
      service_route_rule_(nullptr),
      source_service_route_rule_(source_service_data != nullptr ? new ServiceRouteRule(source_service_data) : nullptr),
      request_flag_(0),
      nearby_disable_(false),
      labels_(nullptr),
      metadata_param_(nullptr),
      circuit_breaker_version_(0) {}

RouteInfo::~RouteInfo() {
  if (service_instances_ != nullptr) {
    delete service_instances_;
    service_instances_ = nullptr;
  }
  if (service_route_rule_ != nullptr) {
    delete service_route_rule_;
    service_route_rule_ = nullptr;
  }
  if (source_service_route_rule_ != nullptr) {
    delete source_service_route_rule_;
    source_service_route_rule_ = nullptr;
  }
}

void RouteInfo::SetIncludeUnhealthyInstances() { request_flag_ = request_flag_ | kGetInstancesRequestIncludeUnhealthy; }

void RouteInfo::SetIncludeCircuitBreakerInstances() {
  request_flag_ = request_flag_ | kGetInstancesRequestIncludeCircuitBreaker;
}

bool RouteInfo::IsIncludeUnhealthyInstances() const { return request_flag_ & kGetInstancesRequestIncludeUnhealthy; }

bool RouteInfo::IsIncludeCircuitBreakerInstances() const {
  return request_flag_ & kGetInstancesRequestIncludeCircuitBreaker;
}

void RouteInfo::SetLables(const std::map<std::string, std::string>& labels) { labels_ = &labels; }

const std::map<std::string, std::string>& RouteInfo::GetLabels() const {
  return labels_ != nullptr ? *labels_ : constants::EmptyStringMap();
}

void RouteInfo::SetMetadataPara(const MetadataRouterParam& metadata_param) { metadata_param_ = &metadata_param; }

const std::map<std::string, std::string>& RouteInfo::GetMetadata() const {
  return metadata_param_ != nullptr ? metadata_param_->metadata_ : constants::EmptyStringMap();
}

MetadataFailoverType RouteInfo::GetMetadataFailoverType() const {
  return metadata_param_ != nullptr ? metadata_param_->failover_type_ : kMetadataFailoverNone;
}

const std::string* RouteInfo::GetCallerSetName() const {
  if (source_service_info_ != nullptr) {
    auto metadata_it = source_service_info_->metadata_.find(constants::kRouterRequestSetNameKey);
    if (metadata_it != source_service_info_->metadata_.end() && !metadata_it->second.empty()) {
      return &metadata_it->second;
    }
  }
  return nullptr;
}

const std::string* RouteInfo::GetCanaryName() const {
  if (source_service_info_ != nullptr) {
    auto metadata_it = source_service_info_->metadata_.find(constants::kRouterRequestCanaryKey);
    if (metadata_it != source_service_info_->metadata_.end()) {
      return &metadata_it->second;
    }
  }
  return nullptr;
}

void RouteInfo::CalculateUnhealthySet(std::set<Instance*>& unhealthy_set) {
  if (!IsIncludeUnhealthyInstances()) {
    unhealthy_set = service_instances_->GetUnhealthyInstances();
  }
  std::map<std::string, Instance*>& instances = service_instances_->GetInstances();
  if (!IsIncludeCircuitBreakerInstances()) {
    if (service_instances_->GetService() == nullptr) {
      POLARIS_LOG(LOG_ERROR, "Service member of %s:%s is null", service_key_.namespace_.c_str(),
                  service_key_.name_.c_str());
      return;
    }
    std::set<std::string> circuit_breaker_set = service_instances_->GetService()->GetCircuitBreakerOpenInstances();
    for (std::set<std::string>::iterator it = circuit_breaker_set.begin(); it != circuit_breaker_set.end(); ++it) {
      std::map<std::string, Instance*>::iterator instance_it = instances.find(*it);
      if (instance_it != instances.end()) {
        unhealthy_set.insert(instance_it->second);
      }
    }
  }
}

bool RouteInfoNotify::IsDataReady(bool use_disk_data) {
  if (all_data_ready_) {
    return true;
  }
  // 有等待的数据未准备好，检查看是否有磁盘加载的数据
  for (int i = 0; i < kDataOrNotifySize; ++i) {
    ServiceDataOrNotify& data_or_notify = data_or_notify_[i];
    if (data_or_notify.service_notify_ != nullptr) {
      if (data_or_notify.service_data_ == nullptr) {
        return false;  // 没有数据
      }
      if (!use_disk_data && !data_or_notify.service_data_->IsAvailable()) {
        return false;  // 不使用磁盘数据时，数据不可用
      }
    }
  }
  return true;
}

ReturnCode RouteInfoNotify::WaitData(timespec& ts) {
  for (int i = 0; i < kDataOrNotifySize; ++i) {
    ServiceDataOrNotify& data_or_notify = data_or_notify_[i];
    if (data_or_notify.service_notify_ != nullptr &&
        (data_or_notify.service_data_ == nullptr || !data_or_notify.service_data_->IsAvailable()) &&
        data_or_notify.service_notify_->WaitDataWithRefUtil(ts, data_or_notify.service_data_) != kReturnOk) {
      return kReturnTimeout;
    }
  }
  all_data_ready_ = true;
  return kReturnOk;
}

ReturnCode RouteInfoNotify::SetDataToRouteInfo(RouteInfo& route_info) {
  if (data_or_notify_[0].service_notify_ != nullptr && data_or_notify_[0].service_data_ != nullptr) {
    // 检查是否服务不存在
    if (data_or_notify_[0].service_data_->GetDataStatus() == kDataNotFound) {
      POLARIS_LOG(LOG_ERROR, "discover instances for service[%s/%s] with service not found",
                  data_or_notify_[0].service_data_->GetServiceKey().namespace_.c_str(),
                  data_or_notify_[0].service_data_->GetServiceKey().name_.c_str());
      return kReturnServiceNotFound;
    }
    route_info.SetServiceInstances(new ServiceInstances(data_or_notify_[0].service_data_));
    data_or_notify_[0].service_data_->DecrementRef();
    data_or_notify_[0].service_data_ = nullptr;
  }
  if (data_or_notify_[1].service_notify_ != nullptr && data_or_notify_[1].service_data_ != nullptr) {
    if (data_or_notify_[1].service_data_->GetDataStatus() == kDataNotFound) {
      POLARIS_LOG(LOG_ERROR, "discover route rule for service[%s/%s] with service not found",
                  data_or_notify_[1].service_data_->GetServiceKey().namespace_.c_str(),
                  data_or_notify_[1].service_data_->GetServiceKey().name_.c_str());
      return kReturnServiceNotFound;
    }
    route_info.SetServiceRouteRule(new ServiceRouteRule(data_or_notify_[1].service_data_));
    data_or_notify_[1].service_data_->DecrementRef();
    data_or_notify_[1].service_data_ = nullptr;
  }
  if (data_or_notify_[2].service_notify_ != nullptr && data_or_notify_[2].service_data_ != nullptr) {
    if (data_or_notify_[2].service_data_->GetDataStatus() == kDataNotFound) {
      POLARIS_LOG(LOG_ERROR, "discover route rule for source service[%s/%s] with service not found",
                  data_or_notify_[2].service_data_->GetServiceKey().namespace_.c_str(),
                  data_or_notify_[2].service_data_->GetServiceKey().name_.c_str());
      return kReturnServiceNotFound;
    }
    route_info.SetSourceServiceRouteRule(new ServiceRouteRule(data_or_notify_[2].service_data_));
    data_or_notify_[2].service_data_->DecrementRef();
    data_or_notify_[2].service_data_ = nullptr;
  }
  return kReturnOk;
}

}  // namespace polaris
