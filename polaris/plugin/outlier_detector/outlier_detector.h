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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_OUTLIER_DETECTOR_OUTLIER_DETECTOR_H_
#define POLARIS_CPP_POLARIS_PLUGIN_OUTLIER_DETECTOR_OUTLIER_DETECTOR_H_

#include <stdint.h>

#include <vector>

#include "polaris/context.h"
#include "polaris/defs.h"

namespace polaris {

class Config;
class LocalRegistry;
class OutlierDetector;

namespace OutlierDetectorConfig {
static const char kChainEnableKey[]   = "enable";
static const bool kChainEnableDefault = false;

static const char kChainPluginListKey[]     = "chain";
static const char kChainPluginListDefault[] = "tcp";

static const char kDetectorIntervalKey[]       = "checkPeriod";
static const uint64_t kDetectorIntervalDefault = 10 * 1000;  // 探活默认时间间隔10s

static const char kHttpRequestPathKey[]     = "path";
static const char kHttpRequestPathDefault[] = "";

static const char kTcpSendPackageKey[]        = "send";
static const char kTcpSendPackageDefault[]    = "";
static const char kTcpReceivePackageKey[]     = "receive";
static const char kTcpReceivePackageDefault[] = "";

static const char kUdpSendPackageKey[]        = "send";
static const char kUdpSendPackageDefault[]    = "";
static const char kUdpReceivePackageKey[]     = "receive";
static const char kUdpReceivePackageDefault[] = "";

static const char kTimeoutKey[]       = "timeout";  // 超时时间毫秒
static const uint64_t kTimeoutDefault = 500;        // 默认500ms
}  // namespace OutlierDetectorConfig

class OutlierDetectorChainImpl : public OutlierDetectorChain {
public:
  OutlierDetectorChainImpl(const ServiceKey& service_key, LocalRegistry* local_registry,
                           CircuitBreakerChain* circuit_breaker_chain);

  virtual ~OutlierDetectorChainImpl();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual ReturnCode DetectInstance();

  virtual std::vector<OutlierDetector*> GetOutlierDetectors();

private:
  ServiceKey service_key_;
  uint64_t detector_ttl_ms_;      // 服务探测周期ms
  uint64_t last_detect_time_ms_;  //  上一次探测时间
  bool enable_;
  LocalRegistry* local_registry_;
  CircuitBreakerChain* circuit_breaker_chain_;  // 用于通知熔断插件链将实例从熔断状态转换为半开状态
  std::vector<OutlierDetector*> outlier_detector_list_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_OUTLIER_DETECTOR_OUTLIER_DETECTOR_H_
