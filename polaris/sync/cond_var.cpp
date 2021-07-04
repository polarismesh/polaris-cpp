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

#include "cond_var.h"

#include <errno.h>
#include <time.h>

#include "utils/time_clock.h"

namespace polaris {

namespace sync {

CondVar::CondVar() { pthread_cond_init(&cond_, NULL); }

CondVar::~CondVar() { pthread_cond_destroy(&cond_); }

int CondVar::Wait(Mutex& mutex, timespec& ts) {
  return pthread_cond_timedwait(&cond_, &mutex.mutex_, &ts);
}

void CondVar::Signal() { pthread_cond_signal(&cond_); }

void CondVar::Broadcast() { pthread_cond_broadcast(&cond_); }

CondVarNotify::CondVarNotify() : notified_(false) {}

CondVarNotify::~CondVarNotify() {}

bool CondVarNotify::Wait(uint64_t timeout) {
  if (notified_) return true;  // 已经就绪直接返回

  timespec ts = Time::CurrentTimeAddWith(timeout);
  return Wait(ts);
}

bool CondVarNotify::Wait(timespec& ts) {
  if (notified_) return true;  // 已经就绪直接返回
  MutexGuard mutex_guard(mutex_);
  while (!notified_) {
    if (cond_.Wait(mutex_, ts) == ETIMEDOUT) {
      break;
    }
  }
  return notified_;
}

void CondVarNotify::Notify() {
  MutexGuard mutex_guard(mutex_);
  notified_ = true;
  cond_.Signal();
}

void CondVarNotify::NotifyAll() {
  MutexGuard mutex_guard(mutex_);
  notified_ = true;
  cond_.Broadcast();
}

};  // namespace sync

}  // namespace polaris
