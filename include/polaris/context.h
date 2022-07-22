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

#ifndef POLARIS_CPP_INCLUDE_POLARIS_CONTEXT_H_
#define POLARIS_CPP_INCLUDE_POLARIS_CONTEXT_H_

#include <map>
#include <string>
#include <vector>

#include "polaris/config.h"
#include "polaris/defs.h"
#include "polaris/noncopyable.h"
#include "polaris/plugin.h"

namespace polaris {

/// @brief Context模式，用于控制Context对象的初始化及释放规则
enum ContextMode {
  kNotInitContext = 0,        // 创建未初始化
  kPrivateContext,            // 私有模式，是否API对象会析构Context
  kShareContext,              // 共享模式，析构API对象不释放Context，需显式释放Context
  kLimitContext,              // 限流模式，会创建限流线程，并校验限流配置
  kShareContextWithoutEngine  // 共享模式，只初始化插件，不创建执行引擎
};

class CircuitBreakerChain;

class HealthCheckerChain {
 public:
  virtual ~HealthCheckerChain() {}

  virtual ReturnCode Init(Config* config, Context* context) = 0;

  virtual ReturnCode DetectInstance(CircuitBreakerChain& circuit_breaker_chain) = 0;

  virtual std::vector<HealthChecker*> GetHealthCheckers() = 0;

  virtual const std::string& GetWhen() const = 0;
};

class ContextImpl;
class Context : Noncopyable {
 public:
  ~Context();

  ContextMode GetContextMode();

  LocalRegistry* GetLocalRegistry();

  ContextImpl* GetContextImpl();

  /// @brief 创建上下文对象
  ///
  /// @param config 配置对象
  /// @param mode 上下文模式，默认共享模式。 @see ContextMode
  /// @return Context* 创建成功：返回创建的上下文对象
  ///                  创建失败：返回NULL，可通过日志查看创建失败的原因
  static Context* Create(Config* config, ContextMode mode = kShareContext);

 private:
  explicit Context(ContextImpl* impl);

  ContextImpl* impl_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_CONTEXT_H_
