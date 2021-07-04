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

#include <pthread.h>
#include <stdint.h>

#include "polaris/noncopyable.h"
#include "sync/mutex.h"

struct timespec;

namespace polaris {
namespace sync {

// 封装pthread的条件变量
class CondVar : Noncopyable {
public:
  CondVar();

  ~CondVar();

  int Wait(Mutex& mutex, timespec& ts);  // 超时等待

  void Signal();  // 通知

  void Broadcast();  // 广播

private:
  pthread_cond_t cond_;
};

// 使用Mutex和CondVar封装一个Notify
class CondVarNotify : Noncopyable {
public:
  CondVarNotify();

  ~CondVarNotify();

  bool Wait(uint64_t timeout);

  bool Wait(timespec& ts);

  void Notify();  // 唤醒一个等待线程

  void NotifyAll();  // 唤醒所有等待的线程

  Mutex& GetMutex() { return mutex_; }

  bool IsNotified() const { return notified_; }

private:
  bool notified_;
  Mutex mutex_;
  CondVar cond_;
};

}  // namespace sync
}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_SYNC_COND_VAR_H_
