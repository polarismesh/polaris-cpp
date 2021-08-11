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

#ifndef POLARIS_CPP_POLARIS_ENGINE_EXECUTOR_H_
#define POLARIS_CPP_POLARIS_ENGINE_EXECUTOR_H_

#include <pthread.h>

#include "polaris/defs.h"
#include "reactor/reactor.h"

namespace polaris {

class Context;

/// @brief 任务执行线程虚基类
class Executor {
public:
  explicit Executor(Context* context);

  virtual ~Executor();

  // 获取线程名字，支持的名字最大长度为16(包括'\0')
  virtual const char* GetName() = 0;

  /// @brief 启动线程前设置一些定时任务，默认不设置任何任务
  virtual void SetupWork() = 0;

  /// @brief 任务循环，默认实现驱动reactor执行
  void WorkLoop();

  // 启动线程执行WorkLoop
  ReturnCode Start();

  // 触发线程停止并等待线程退出
  ReturnCode StopAndWait();

  Reactor& GetReactor() { return reactor_; }

  static void* ThreadFunction(void* arg);

protected:
  Context* context_;
  Reactor reactor_;
  pthread_t tid_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_ENGINE_EXECUTOR_H_
