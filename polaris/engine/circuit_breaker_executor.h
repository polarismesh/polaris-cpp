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

#ifndef POLARIS_CPP_POLARIS_ENGINE_CIRCUIT_BREAKER_EXECUTOR_H_
#define POLARIS_CPP_POLARIS_ENGINE_CIRCUIT_BREAKER_EXECUTOR_H_

#include "engine/executor.h"

namespace polaris {

class Context;
class MetricConnector;

/// @brief 主任务
///
/// 执行如下任务：
///   - 执行实例级别熔断
///   - 执行Set级别熔断
class CircuitBreakerExecutor : public Executor {
 public:
  explicit CircuitBreakerExecutor(Context* context);

  // 仅供单元测试使用，正常逻辑代码勿用
  void SetMetricConnector(MetricConnector* connector);

  virtual ~CircuitBreakerExecutor();

  // 获取线程名字
  virtual const char* GetName() { return "circuit_break"; }

  virtual void SetupWork();

  MetricConnector* GetMetricConnector() { return metric_connector_; }

  // 定期执行ReportClient
  static void TimingCircuitBreak(CircuitBreakerExecutor* executor);

 private:
  MetricConnector* metric_connector_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_ENGINE_CIRCUIT_BREAKER_EXECUTOR_H_
