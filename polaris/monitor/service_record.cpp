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

#include "service_record.h"

#include <stddef.h>
#include <v1/request.pb.h>

#include "logger.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

namespace polaris {

ServiceRecord::ServiceRecord() : report_id_(0) {}

ServiceRecord::~ServiceRecord() {}

void ServiceRecord::ServiceDataUpdate(ServiceData* service_data) {
  if (service_data->GetDataStatus() == kDataNotFound) {  // 服务在远程被删除
    ServiceDataDelete(service_data->GetServiceKey(), service_data->GetDataType());
    return;
  }
  uint64_t current_time = Time::GetCurrentTimeMs();  // 锁外获取时间，防止等锁后时间不准
  const ServiceKey& service_key  = service_data->GetServiceKey();
  ::v1::RevisionHistory* history = NULL;
  uint64_t seq_id                = 0;
  sync::MutexGuard mutex_guard(lock_);  // 加锁
  ServiceRecordId& record_id = service_record_id_map_[service_key];
  ::v1::ServiceInfo& info    = service_info_map_[service_key];
  if (service_data->GetDataType() == kServiceDataInstances) {
    if (service_data->GetDataStatus() != kDataInitFromDisk) {
      record_id.instances_id_++;  // 递增记录ID
    } else {                      // 从磁盘加载的数据record id使用1上传
      POLARIS_ASSERT(record_id.instances_id_ == 1);
    }
    history = info.mutable_instances_history()->add_revision();
    seq_id  = record_id.instances_id_;
  } else if (service_data->GetDataType() == kServiceDataRouteRule) {
    if (service_data->GetDataStatus() != kDataInitFromDisk) {
      record_id.route_id_++;  // 递增记录ID
    } else {                  // 从磁盘加载的数据record id使用1上传
      POLARIS_ASSERT(record_id.route_id_ == 1);
    }
    history = info.mutable_routing_history()->add_revision();
    seq_id  = record_id.route_id_;
  } else if (service_data->GetDataType() == kServiceDataRateLimit) {
    if (service_data->GetDataStatus() != kDataInitFromDisk) {
      record_id.rate_limit_id_++;  // 递增记录ID
    } else {                       // 从磁盘加载的数据record id使用1上传
      POLARIS_ASSERT(record_id.rate_limit_id_ == 1);
    }
    history = info.mutable_rate_limit_history()->add_revision();
    seq_id  = record_id.rate_limit_id_;
  } else if (service_data->GetDataType() == kCircuitBreakerConfig) {
    return;  // TODO 目前不支持
  } else {   // 数据类型错误
    POLARIS_ASSERT(false);
  }
  Time::Uint64ToTimestamp(current_time, history->mutable_time());
  history->set_change_seq(seq_id);
  history->set_revision(service_data->GetRevision());
}

void ServiceRecord::ServiceDataDelete(const ServiceKey& service_key, ServiceDataType data_type) {
  uint64_t current_time = Time::GetCurrentTimeMs();  // 锁外获取时间，防止等锁后时间不准
  ::v1::RevisionHistory* history = NULL;
  uint64_t seq_id                = 0;
  sync::MutexGuard mutex_guard(lock_);  // 加锁
  ServiceRecordId& record_id = service_record_id_map_[service_key];
  ::v1::ServiceInfo& info    = service_info_map_[service_key];
  if (data_type == kServiceDataInstances) {
    seq_id = record_id.instances_id_ + 1;  // 递增记录ID
    info.set_instance_eliminated(true);
    history                 = info.mutable_instances_history()->add_revision();
    record_id.instances_id_ = 1;
  } else if (data_type == kServiceDataRouteRule) {
    seq_id = record_id.route_id_ + 1;  // 递增记录ID
    info.set_routing_eliminated(true);
    history             = info.mutable_routing_history()->add_revision();
    record_id.route_id_ = 1;
  } else if (data_type == kServiceDataRateLimit) {
    seq_id = record_id.rate_limit_id_ + 1;  // 递增记录ID
    info.set_rate_limit_eliminated(true);
    history                  = info.mutable_rate_limit_history()->add_revision();
    record_id.rate_limit_id_ = 1;
  } else if (data_type == kCircuitBreakerConfig) {
    return;  // TODO 目前不支持
  } else {   // 数据类型错误
    POLARIS_ASSERT(false);
  }
  Time::Uint64ToTimestamp(current_time, history->mutable_time());
  history->set_change_seq(seq_id);
  if (record_id.instances_id_ == 1 && record_id.route_id_ == 1 && record_id.rate_limit_id_ == 1) {
    service_record_id_map_.erase(service_key);  // 数据都删除了
  }
}

void ServiceRecord::InstanceCircuitBreak(const ServiceKey& service_key,
                                         const std::string& instance_id,
                                         CircuitChangeRecord* record) {
  sync::MutexGuard mutex_guard(lock_);  // 加锁
  InstanceRecords& instance_records          = instance_records_map_[service_key];
  std::vector<CircuitChangeRecord*>& records = instance_records.circuit_record_[instance_id];
  records.push_back(record);
}

void ServiceRecord::SetCircuitBreak(const ServiceKey& service_key, const std::string& set_label_id,
                                    CircuitChangeRecord* record) {
  sync::MutexGuard mutex_guard(lock_);  // 加锁
  SetRecords& set_records                    = set_records_map_[service_key];
  std::vector<CircuitChangeRecord*>& records = set_records.circuit_record_[set_label_id];
  records.push_back(record);
}

void ServiceRecord::InstanceRecoverAll(const ServiceKey& service_key, RecoverAllRecord* record) {
  sync::MutexGuard mutex_guard(lock_);  // 加锁
  InstanceRecords& instance_records = instance_records_map_[service_key];
  instance_records.recover_record_.push_back(record);
}

void ServiceRecord::ReportServiceCache(std::map<ServiceKey, ::v1::ServiceInfo>& report_data) {
  do {
    sync::MutexGuard mutex_guard(lock_);  // 加锁
    report_data.swap(service_info_map_);
  } while (false);

  if (report_data.empty()) {
    POLARIS_STAT_LOG(LOG_INFO, "no service cache info to send this period");
    return;
  }
  for (std::map<ServiceKey, v1::ServiceInfo>::iterator it = report_data.begin();
       it != report_data.end(); ++it) {
    v1::ServiceInfo& service_info = it->second;
    service_info.set_id(StringUtils::TypeToStr<uint64_t>(report_id_++));
    service_info.set_namespace_(it->first.namespace_);
    service_info.set_service(it->first.name_);
  }
}

void ServiceRecord::ReportCircuitStat(std::map<ServiceKey, InstanceRecords>& report_data) {
  do {
    sync::MutexGuard mutex_guard(lock_);  // 加锁
    report_data.swap(instance_records_map_);
  } while (false);
  if (report_data.empty()) {
    POLARIS_STAT_LOG(LOG_INFO, "no instance circuit stat data to send this period");
    return;
  }
}

void ServiceRecord::ReportSetCircuitStat(std::map<ServiceKey, SetRecords>& report_data) {
  do {
    sync::MutexGuard mutex_guard(lock_);  // 加锁
    report_data.swap(set_records_map_);
  } while (false);
  if (report_data.empty()) {
    POLARIS_STAT_LOG(LOG_INFO, "no set circuit stat data to send this period");
    return;
  }
}

}  // namespace polaris
