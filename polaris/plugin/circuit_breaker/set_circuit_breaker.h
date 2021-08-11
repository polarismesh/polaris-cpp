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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_SET_CIRCUIT_BREAKER_H_
#define POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_SET_CIRCUIT_BREAKER_H_

#include "polaris/defs.h"
#include "polaris/plugin.h"

namespace v1 {
class CircuitBreaker;
class DestinationSet;
}  // namespace v1

namespace polaris {

class CircuitBreakSetChainData;
class Config;
class Context;
class MetricWindowManager;
class ServiceData;

class SetCircuitBreakerImpl : public SetCircuitBreaker {
public:
  explicit SetCircuitBreakerImpl(const ServiceKey& service_key);
  virtual ~SetCircuitBreakerImpl();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual ReturnCode RealTimeCircuitBreak(const InstanceGauge& instance_gauge);

  virtual ReturnCode TimingCircuitBreak();

private:
  ReturnCode GetCbPConfPbFromLocalRegistry(ServiceData*& service_data,
                                           v1::CircuitBreaker*& pb_conf);
  ReturnCode MatchDestinationSet(v1::CircuitBreaker* pb_conf, const InstanceGauge& instance_gauge,
                                 v1::DestinationSet*& dst_conf);

private:
  ServiceKey service_key_;
  Context* context_;
  bool enable_;

  MetricWindowManager* windows_manager_;
  CircuitBreakSetChainData* chain_data_impl_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_SET_CIRCUIT_BREAKER_H_
