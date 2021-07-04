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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_CIRCUIT_BREAKER_H_
#define POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_CIRCUIT_BREAKER_H_

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "polaris/context.h"
#include "polaris/defs.h"
#include "polaris/plugin.h"
#include "sync/mutex.h"

namespace polaris {

class Config;
class ServiceRecord;
struct CircuitChangeRecord;

namespace CircuitBreakerConfig {
static const char kChainEnableKey[]   = "enable";
static const bool kChainEnableDefault = true;

static const char kChainCheckPeriodKey[]       = "checkPeriod";
static const uint64_t kChainCheckPeriodDefault = 500;

// 用于传入是否开启探测插件
static const char kDetectorDisableKey[]   = "detectorDisable";
static const bool kDetectorDisableDefault = true;

static const char kChainPluginListKey[]     = "chain";
static const char kChainPluginListDefault[] = "errorCount, errorRate";

static const char kContinuousErrorThresholdKey[]  = "continuousErrorThreshold";
static const int kContinuousErrorThresholdDefault = 10;

static const char kHalfOpenSleepWindowKey[]       = "sleepWindow";
static const uint64_t kHalfOpenSleepWindowDefault = 30 * 1000;

static const char kRequestCountAfterHalfOpenKey[]  = "requestCountAfterHalfOpen";
static const int kRequestCountAfterHalfOpenDefault = 10;

static const char kSuccessCountAfterHalfOpenKey[]  = "successCountAfterHalfOpen";
static const int kSuccessCountAfterHalfOpenDefault = 8;

static const char kRequestVolumeThresholdKey[]  = "requestVolumeThreshold";
static const int kRequestVolumeThresholdDefault = 10;

static const char kErrorRateThresholdKey[]    = "errorRateThreshold";
static const float kErrorRateThresholdDefault = 0.5;

static const char kMetricStatTimeWindowKey[]       = "metricStatTimeWindow";
static const uint64_t kMetricStatTimeWindowDefault = 60 * 1000;

static const char kMetricNumBucketsKey[]  = "metricNumBuckets";
static const int kMetricNumBucketsDefault = 12;

static const char kMetricExpiredTimeKey[]       = "metricExpiredTime";
static const uint64_t kMetricExpiredTimeDefault = 60 * 60 * 1000;
}  // namespace CircuitBreakerConfig

struct CircuitBreakerPluginData {
  std::string plugin_name;
  int request_after_half_open;
};

struct CircuitBreakerChainStatus {
  CircuitBreakerChainStatus()
      : status(kCircuitBreakerClose), owner_plugin_index(0), change_seq_id(0) {}
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

  CircuitChangeRecord* TranslateStatus(int plugin_index, const std::string& instance_id,
                                       CircuitBreakerStatus from, CircuitBreakerStatus to);

  void CheckAndSyncToLocalRegistry(LocalRegistry* local_registry, const ServiceKey& service_key);

private:
  std::vector<CircuitBreakerPluginData> plugin_data_map_;

  uint64_t last_update_version_;
  uint64_t current_version_;
  sync::Mutex lock_;
  std::map<std::string, CircuitBreakerChainStatus> chain_status_map_;
};

// 针对每个插件封装熔断链的数据
class InstancesCircuitBreakerStatusImpl : public InstancesCircuitBreakerStatus {
public:
  InstancesCircuitBreakerStatusImpl(CircuitBreakerChainData* chain_data, int plugin_index,
                                    ServiceKey& service_key, ServiceRecord* service_record,
                                    bool auto_half_open_enable);

  virtual ~InstancesCircuitBreakerStatusImpl();

  virtual bool TranslateStatus(const std::string& instance_id, CircuitBreakerStatus from_status,
                               CircuitBreakerStatus to_status);

  virtual bool AutoHalfOpenEnable() { return auto_half_open_enable_; }

private:
  ServiceKey& service_key_;
  ServiceRecord* service_record_;
  CircuitBreakerChainData* chain_data_;
  int plugin_index_;
  bool auto_half_open_enable_;
};

// ============================================================================
// 熔断插件链
class CircuitBreakerChainImpl : public CircuitBreakerChain {
public:
  CircuitBreakerChainImpl(const ServiceKey& service_key, LocalRegistry* local_registry,
                          bool auto_half_open_enable);

  virtual ~CircuitBreakerChainImpl();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual ReturnCode RealTimeCircuitBreak(const InstanceGauge& instance_gauge);

  virtual ReturnCode TimingCircuitBreak();

  virtual std::vector<CircuitBreaker*> GetCircuitBreakers();

  virtual ReturnCode TranslateStatus(const std::string& instance_id,
                                     CircuitBreakerStatus from_status,
                                     CircuitBreakerStatus to_status);

  virtual void PrepareServicePbConfTrigger();

private:
  ReturnCode InitPlugin(Config* config, Context* context, const std::string& plugin_name);

private:
  ServiceKey service_key_;
  bool enable_;
  uint64_t check_period_;
  uint64_t last_check_time_;
  std::vector<CircuitBreaker*> circuit_breaker_list_;

  CircuitBreakerChainData* chain_data_;
  bool auto_half_open_enable_;
  std::vector<InstancesCircuitBreakerStatusImpl*> instances_status_list_;

  LocalRegistry* local_registry_;

  SetCircuitBreaker* set_circuit_breaker_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_CIRCUIT_BREAKER_H_
