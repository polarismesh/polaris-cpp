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

#include "utils/fork.h"

#include <pthread.h>
#include <stdint.h>

#include <atomic>
#include <mutex>

#include "utils/time_clock.h"

namespace polaris {

/// @brief 记录当前第几次fork出来的子进程
int polaris_fork_count = 0;

extern int g_custom_clock_ref_count;
extern std::mutex g_custom_clock_lock;
extern pthread_t g_custom_clock_update_tid;

/// @brief 准备fork回调函数
void ForkPrepare() { g_custom_clock_lock.lock(); }

/// @brief fork完成父进程回调函数
void ForkPostParent() { g_custom_clock_lock.unlock(); }

/// @brief 让每个进程只在首次常见API对象时设置一次fork回调
static bool fork_callback_setup = false;

/// @brief Fork完成子进程回调函数
void ForkPostChild() {
  Time::SetDefaultTimeFunc();  // 还原成真实的时钟函数
  g_custom_clock_ref_count = 0;
  g_custom_clock_update_tid = 0;

  polaris_fork_count++;  // 增加
  fork_callback_setup = false;

  g_custom_clock_lock.unlock();
}

void SetupCallbackAtfork() {
  if (!fork_callback_setup) {
    pthread_atfork(ForkPrepare, ForkPostParent, ForkPostChild);
    fork_callback_setup = true;
  }
}

}  // namespace polaris
