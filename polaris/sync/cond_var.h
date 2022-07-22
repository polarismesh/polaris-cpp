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

#ifndef POLARIS_CPP_POLARIS_SYNC_COND_VAR_H_
#define POLARIS_CPP_POLARIS_SYNC_COND_VAR_H_

#include <stdint.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "polaris/noncopyable.h"

namespace polaris {
namespace sync {

// 使用Mutex和CondVar封装一个Notify
class CondVarNotify : Noncopyable {
 public:
  CondVarNotify();

  ~CondVarNotify();

  bool WaitFor(uint64_t timeout);

  bool WaitUntil(const std::chrono::steady_clock::time_point& time_point);

  void Notify();  // 唤醒一个等待线程

  void NotifyAll();  // 唤醒所有等待的线程

  std::mutex& GetMutex() { return mutex_; }

  bool IsNotified() const { return notified_; }

 private:
  bool notified_;
  std::mutex mutex_;
  std::condition_variable cond_;
};

}  // namespace sync
}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_SYNC_COND_VAR_H_
