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

#include <functional>

#include "polaris/defs.h"
#include "polaris/plugin.h"

namespace polaris {

class InstancesCircuitBreakerStatus;

using InstanceExistChecker = std::function<bool(const std::string& instance_id)>;

/// @brief 扩展点接口：节点熔断
class CircuitBreaker : public Plugin {
 public:
  /// @brief 析构函数
  virtual ~CircuitBreaker() {}

  /// @brief 通过配置进行初始化
  virtual ReturnCode Init(Config* config, Context* context) = 0;

  virtual int RequestAfterHalfOpen() = 0;

  virtual ReturnCode DetectToHalfOpen(const std::string& instance_id) = 0;

  virtual ReturnCode RealTimeCircuitBreak(const InstanceGauge& instance_gauge,
                                          InstancesCircuitBreakerStatus* instances_status) = 0;
  /// @brief 进行节点的熔断
  ///
  /// @param service 服务缓存
  /// @param stat_info 服务统计信息
  /// @param instances 返回被熔断的节点
  /// @return ReturnCode 调用返回码
  virtual ReturnCode TimingCircuitBreak(InstancesCircuitBreakerStatus* instances_status) = 0;

  /// @brief 清理过期服务实例状态
  virtual void CleanStatus(InstancesCircuitBreakerStatus* instances_status, InstanceExistChecker& exist_checker) = 0;
};

// @brief 扩展点接口：Set熔断
class SetCircuitBreaker : public Plugin {
 public:
  /// @brief 析构函数
  virtual ~SetCircuitBreaker() {}

  /// @brief 通过配置进行初始化
  virtual ReturnCode Init(Config* config, Context* context) = 0;

  virtual ReturnCode RealTimeCircuitBreak(const InstanceGauge& instance_gauge) = 0;

  virtual ReturnCode TimingCircuitBreak() = 0;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_CIRCUIT_BREAKER_H_
