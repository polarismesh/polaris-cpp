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

#ifndef POLARIS_CPP_POLARIS_MONITOR_SERVICE_RECORD_H_
#define POLARIS_CPP_POLARIS_MONITOR_SERVICE_RECORD_H_

#include <stdint.h>
#include <v1/request.pb.h>

#include <iosfwd>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "polaris/defs.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "sync/mutex.h"

namespace polaris {

struct ServiceRecordId {
  ServiceRecordId() : instances_id_(1), route_id_(1), rate_limit_id_(1) {}
  uint64_t instances_id_;
  uint64_t route_id_;
  uint64_t rate_limit_id_;
};

// 熔断状态变更记录
struct CircuitChangeRecord {
  uint64_t change_time_;
  uint32_t change_seq_;
  CircuitBreakerStatus from_;
  CircuitBreakerStatus to_;
  std::string reason_;
  std::string circuit_breaker_conf_id_;
};

// 全死全活记录
struct RecoverAllRecord {
  RecoverAllRecord(uint64_t recover_time, const std::string& info, bool status)
      : recover_time_(recover_time), cluster_info_(info), recover_status_(status) {}
  uint64_t recover_time_;
  std::string cluster_info_;  // 发送全死全活的信息
  bool recover_status_;       // true 发送全死全活，false为结束全死全活
};

struct InstanceRecords {
  ~InstanceRecords() {
    for (std::map<std::string, std::vector<CircuitChangeRecord*> >::iterator it =
             circuit_record_.begin();
         it != circuit_record_.end(); ++it) {
      for (std::size_t i = 0; i < it->second.size(); ++i) {
        delete it->second[i];
      }
    }
    for (std::size_t i = 0; i < recover_record_.size(); ++i) {
      delete recover_record_[i];
    }
  }
  std::map<std::string, std::vector<CircuitChangeRecord*> > circuit_record_;
  std::vector<RecoverAllRecord*> recover_record_;
};

struct SetRecords {
  ~SetRecords() {
    for (std::map<std::string, std::vector<CircuitChangeRecord*> >::iterator it =
             circuit_record_.begin();
         it != circuit_record_.end(); ++it) {
      for (std::size_t i = 0; i < it->second.size(); ++i) {
        delete it->second[i];
      }
    }
  }

  std::map<std::string, std::vector<CircuitChangeRecord*> > circuit_record_;
};

class ServiceRecord {
public:
  ServiceRecord();

  ~ServiceRecord();

  void ServiceDataUpdate(ServiceData* service_data);

  void ServiceDataDelete(const ServiceKey& service_key, ServiceDataType data_type);

  void InstanceCircuitBreak(const ServiceKey& service_key, const std::string& instance_id,
                            CircuitChangeRecord* record);

  void SetCircuitBreak(const ServiceKey& service_key, const std::string& set_label_id,
                       CircuitChangeRecord* record);

  void InstanceRecoverAll(const ServiceKey& service_key, RecoverAllRecord* record);

  void ReportServiceCache(std::map<ServiceKey, ::v1::ServiceInfo>& report_data);

  void ReportCircuitStat(std::map<ServiceKey, InstanceRecords>& report_data);

  void ReportSetCircuitStat(std::map<ServiceKey, SetRecords>& report_data);

private:
  uint64_t report_id_;
  sync::Mutex lock_;
  std::map<ServiceKey, ServiceRecordId> service_record_id_map_;
  std::map<ServiceKey, ::v1::ServiceInfo> service_info_map_;
  std::map<ServiceKey, InstanceRecords> instance_records_map_;
  std::map<ServiceKey, SetRecords> set_records_map_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MONITOR_SERVICE_RECORD_H_
