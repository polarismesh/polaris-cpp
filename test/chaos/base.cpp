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

#include "base.h"

#include <pthread.h>

extern bool signal_received;

namespace polaris {

void* ThreadFun(void* args) {
  ChaosBase* chaos = static_cast<ChaosBase*>(args);
  chaos->Run();

  chaos->TearDown();
  return nullptr;
}

bool ChaosBase::Start() {
  if (!this->SetUp()) {
    return false;
  }
  int rc = pthread_create(&tid_, nullptr, ThreadFun, this);
  return rc == 0;
}

void ChaosBase::Stop() {
  stop_received_ = true;
  if (tid_ != 0) {
    pthread_join(tid_, nullptr);
  }
}

}  // namespace polaris
