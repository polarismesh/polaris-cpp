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

#include "cache/rcu_time.h"

#include <stddef.h>
#include "logger.h"
#include "utils/time_clock.h"

namespace polaris {

ThreadTimeMgr::ThreadTimeMgr() {
  thread_time_key_ = 0;
  int rc = pthread_key_create(&thread_time_key_, &OnThreadExit);
  POLARIS_ASSERT(rc == 0);
}

ThreadTimeMgr::~ThreadTimeMgr() {
  pthread_key_delete(thread_time_key_);
  for (std::set<ThreadTime*>::iterator it = thread_time_set_.begin(); it != thread_time_set_.end(); ++it) {
    delete (*it);
  }
}

void ThreadTimeMgr::RcuEnter() {
  ThreadTime* thread_time = static_cast<ThreadTime*>(pthread_getspecific(thread_time_key_));
  if (thread_time != nullptr) {
    thread_time->thread_time_.store(Time::GetCoarseSteadyTimeMs(), std::memory_order_release);
    return;
  }
  thread_time = new ThreadTime(Time::GetCoarseSteadyTimeMs(), this);
  pthread_setspecific(thread_time_key_, thread_time);
  const std::lock_guard<std::mutex> mutex_guard(lock_);
  thread_time_set_.insert(thread_time);
}

void ThreadTimeMgr::RcuExit() {
  ThreadTime* thread_time = static_cast<ThreadTime*>(pthread_getspecific(thread_time_key_));
  if (thread_time != nullptr) {
    thread_time->thread_time_.store(Time::kMaxTime, std::memory_order_release);
  }
}

uint64_t ThreadTimeMgr::MinTime() {
  uint64_t min_time = Time::GetCoarseSteadyTimeMs();
  const std::lock_guard<std::mutex> mutex_guard(lock_);
  for (std::set<ThreadTime*>::iterator it = thread_time_set_.begin(); it != thread_time_set_.end(); ++it) {
    uint64_t thread_time = (*it)->thread_time_.load(std::memory_order_acquire);  // 先获取时间再比较
    if (thread_time < min_time) {
      min_time = thread_time;
    }
  }
  return min_time;
}

void ThreadTimeMgr::OnThreadExit(void* ptr) {
  if (ptr == nullptr) {
    return;
  }
  ThreadTime* thread_time = static_cast<ThreadTime*>(ptr);
  ThreadTimeMgr* mgr = static_cast<ThreadTimeMgr*>(thread_time->mgr_ptr_);

  mgr->lock_.lock();
  mgr->thread_time_set_.erase(thread_time);
  mgr->lock_.unlock();
  delete thread_time;
}

}  // namespace polaris
