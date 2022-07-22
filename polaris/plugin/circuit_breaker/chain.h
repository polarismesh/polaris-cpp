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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_CHAIN_H_
#define POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_CHAIN_H_

#include <stdint.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "plugin/circuit_breaker/circuit_breaker.h"
#include "polaris/context.h"
#include "polaris/defs.h"

namespace polaris {

class ServiceRecord;
struct CircuitChangeRecord;
struct CircuitBreakerPluginData {
  std::string plugin_name;
  int request_after_half_open;
};

struct CircuitBreakerChainStatus {
  CircuitBreakerChainStatus() : status(kCircuitBreakerClose), owner_plugin_index(0), change_seq_id(0) {}
  CircuitBreakerStatus status;
  int owner_plugin_index;
  uint32_t change_seq_id;
};

inline const char* CircuitBreakerStatusToStr(const CircuitBreakerStatus& status) {
  switch (status) {
    case kCircuitBreakerClose:
      return "Close";
    case kCircuitBreakerHalfOpen:
      return "Half-Open";
    case kCircuitBreakerOpen:
      return "Open";
    case kCircuitBreakerPreserved:
      return "Preserved";
    default:
      return "Unknow";
  }
}

class CircuitBreakerChainData {
 public:
  CircuitBreakerChainData();

  ~CircuitBreakerChainData();

  void AppendPluginData(const CircuitBreakerPluginData& sub_data);

  CircuitChangeRecord* TranslateStatus(int plugin_index, const std::string& instance_id, CircuitBreakerStatus from,
                                       CircuitBreakerStatus to);

  bool CheckAndSyncToLocalRegistry(LocalRegistry* local_registry, const ServiceKey& service_key);

  uint64_t GetCurrentVersion() const { return current_version_; }

 private:
  std::vector<CircuitBreakerPluginData> plugin_data_map_;

  uint64_t last_update_version_;
  uint64_t current_version_;
  std::mutex lock_;
  std::map<std::string, CircuitBreakerChainStatus> chain_status_map_;
};

// 针对每个插件封装熔断链的数据
class InstancesCircuitBreakerStatus {
 public:
  InstancesCircuitBreakerStatus(CircuitBreakerChainData* chain_data, int plugin_index, ServiceKey& service_key,
                                ServiceRecord* service_record, bool auto_half_open_enable);

  ~InstancesCircuitBreakerStatus();

  bool TranslateStatus(const std::string& instance_id, CircuitBreakerStatus from_status,
                       CircuitBreakerStatus to_status);

  bool AutoHalfOpenEnable() { return auto_half_open_enable_; }

 private:
  ServiceKey& service_key_;
  ServiceRecord* service_record_;
  CircuitBreakerChainData* chain_data_;
  int plugin_index_;
  bool auto_half_open_enable_;
};

// ============================================================================
// 熔断插件链
class CircuitBreakerChain {
 public:
  explicit CircuitBreakerChain(const ServiceKey& service_key);

  virtual ~CircuitBreakerChain();

  ReturnCode Init(Config* config, Context* context, const std::string& health_check_when);

  ReturnCode RealTimeCircuitBreak(const InstanceGauge& instance_gauge);

  ReturnCode TimingCircuitBreak(InstanceExistChecker& exist_checker);

  std::vector<CircuitBreaker*> GetCircuitBreakers();

  virtual bool TranslateStatus(const std::string& instance_id, CircuitBreakerStatus from_status,
                               CircuitBreakerStatus to_status);

  void SubmitUpdateCache(uint64_t circuit_breaker_version);

  void PrepareServicePbConfTrigger();

 private:
  ReturnCode InitPlugin(Config* config, Context* context, const std::string& plugin_name);

 private:
  ServiceKey service_key_;
  Context* context_;
  bool enable_;
  uint64_t check_period_;
  uint64_t next_check_time_;
  std::vector<CircuitBreaker*> circuit_breaker_list_;

  CircuitBreakerChainData* chain_data_;
  std::string health_check_when_;
  std::vector<InstancesCircuitBreakerStatus*> instances_status_list_;

  SetCircuitBreaker* set_circuit_breaker_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_CHAIN_H_
