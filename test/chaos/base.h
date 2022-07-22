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

#ifndef POLARIS_CPP_TEST_CHAOS_BASE_H_
#define POLARIS_CPP_TEST_CHAOS_BASE_H_

#include <pthread.h>

#include "polaris/log.h"

namespace polaris {

#define ERROR_PREFIX "chaos error: "
#define CHAOS_ERROR(format, ...) GetLogger()->Log(POLARIS_ERROR, ERROR_PREFIX format, ##__VA_ARGS__);
#define CHAOS_INFO(...) GetLogger()->Log(POLARIS_INFO, ##__VA_ARGS__);

class ChaosBase {
 public:
  ChaosBase() : tid_(0), stop_received_(false) {}

  virtual ~ChaosBase() {}

  virtual bool SetUp() = 0;

  virtual void Run() = 0;

  virtual void TearDown() = 0;

  bool Start();

  void Stop();

 protected:
  pthread_t tid_;
  bool stop_received_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_CHAOS_BASE_H_