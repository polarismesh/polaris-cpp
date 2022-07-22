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

#include "engine/executor.h"

#include <features.h>
#include <stddef.h>

#include "logger.h"

namespace polaris {

Executor::Executor(Context* context) : context_(context), tid_(0) {}

Executor::~Executor() {
  StopAndWait();
  context_ = nullptr;
}

void Executor::WorkLoop() { reactor_.Run(); }

ReturnCode Executor::Start() {
  POLARIS_ASSERT(tid_ == 0);
  if (pthread_create(&tid_, nullptr, ThreadFunction, this) != 0) {
    POLARIS_LOG(LOG_ERROR, "create %s task thread failed", GetName());
    return kReturnInvalidState;
  }
#if defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 12) && !defined(COMPILE_FOR_PRE_CPP11)
  // pthread_setname_np 支持的名字最大长度为16(包括'\0')
  pthread_setname_np(tid_, GetName());  // glibc >=2.12版本才有这个函数
#endif
  return kReturnOk;
}

void* Executor::ThreadFunction(void* arg) {
  Executor* executor = static_cast<Executor*>(arg);
  executor->SetupWork();
  executor->WorkLoop();
  return nullptr;
}

ReturnCode Executor::StopAndWait() {
  reactor_.Stop();
  if (tid_ > 0) {
    pthread_join(tid_, nullptr);
    tid_ = 0;
  }
  return kReturnOk;
}

}  // namespace polaris
