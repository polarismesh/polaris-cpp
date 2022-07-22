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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_SET_CIRCUIT_BREAKER_CHAIN_DATA_H_
#define POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_SET_CIRCUIT_BREAKER_CHAIN_DATA_H_

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <map>
#include <string>
#include <utility>

#include "plugin/circuit_breaker/circuit_breaker.h"
#include "polaris/defs.h"
#include "polaris/model.h"

namespace v1 {
class DestinationSet;
class MetricResponse;
}  // namespace v1

namespace polaris {

class MetricWindowManager;
class ServiceRecord;
struct CircuitChangeRecord;

struct CircuitBreakerSetComputeResult {
  CircuitBreakerSetComputeResult()
      : status(kCircuitBreakerClose),
        half_open_release_percent(0),
        open_status_begin_time(0),
        fail_rate_(0),
        total_count(0) {}

  CircuitBreakerStatus status;
  float half_open_release_percent;
  uint64_t open_status_begin_time;
  uint32_t fail_rate_;
  uint64_t total_count;
  std::string status_reason;
};

class CircuitBreakSetChainData : public ServiceBase {
 public:
  CircuitBreakSetChainData(ServiceKey& service_key, LocalRegistry* local_registry, MetricWindowManager* window_manager,
                           ServiceRecord* service_record);

  virtual ~CircuitBreakSetChainData();

  ReturnCode JudgeAndTranslateStatus(const v1::MetricResponse& resp, const std::string& subset_label_id,
                                     const v1::DestinationSet* conf, const std::string& cb_id);

  ReturnCode CheckAndSyncToRegistry();

  void MarkDeleted() { is_deleted_ = true; }

  inline SetCircuitBreakerUnhealthyInfo* GetSubSetUnhealthyInfo(const std::string& key) {
    std::map<std::string, SetCircuitBreakerUnhealthyInfo*>::iterator iter;
    iter = unhealthy_sets_.find(key);
    if (iter != unhealthy_sets_.end()) {
      return iter->second;
    } else {
      return nullptr;
    }
  }

  std::atomic<bool> is_deleted_;

 private:
  ReturnCode ComputeUnhealthyInfo(const v1::MetricResponse& resp, const v1::DestinationSet* conf,
                                  CircuitBreakerSetComputeResult* info);

  ReturnCode ChangeSubsetOneLabel(CircuitBreakerSetComputeResult* new_info, const v1::DestinationSet* conf,
                                  uint64_t time_now, const std::string& set_label_id, const std::string& cb_id);

  ReturnCode CircuitBreakSubsetAll(const std::string& set_label_id, uint64_t time_now, const std::string& cb_id,
                                   CircuitBreakerSetComputeResult* new_info);

  bool JudgeOpenTranslate(SetCircuitBreakerUnhealthyInfo* info, const v1::DestinationSet* conf, uint64_t time_now);

  bool JudgeHalfOpenTranslate(SetCircuitBreakerUnhealthyInfo* info, CircuitBreakerSetComputeResult* new_info,
                              const v1::DestinationSet* conf, uint64_t time_now);

  bool JudgePreservedTranslate(SetCircuitBreakerUnhealthyInfo* info, CircuitBreakerSetComputeResult* new_info,
                               uint64_t time_now);

 private:
  void ComputeTypeCount(const v1::MetricResponse& resp, uint64_t* total_count, uint64_t* err_count,
                        uint64_t* slow_count, std::map<std::string, uint64_t>* specific_err_);

  CircuitChangeRecord* ChangeRecordValues(const std::string& set_label_id, uint64_t change_time,
                                          CircuitBreakerStatus from, CircuitBreakerStatus to,
                                          std::string& status_reason);

 private:
  friend class MetricQueryCallback;
  ServiceKey& service_key_;
  LocalRegistry* local_registry_;

  pthread_rwlock_t rwlock_;
  std::atomic<uint64_t> version_;
  std::map<std::string, SetCircuitBreakerUnhealthyInfo*> unhealthy_sets_;

  MetricWindowManager* windows_manager_;
  uint64_t windows_info_version_;

  ServiceRecord* service_record_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_SET_CIRCUIT_BREAKER_CHAIN_DATA_H_
