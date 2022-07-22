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

#ifndef POLARIS_CPP_POLARIS_CACHE_RCU_TIME_H_
#define POLARIS_CPP_POLARIS_CACHE_RCU_TIME_H_

#include <pthread.h>
#include <stdint.h>

#include <atomic>
#include <mutex>
#include <set>

namespace polaris {

struct ThreadTime {
  ThreadTime(uint64_t thread_time, void* mgr_ptr) : thread_time_(thread_time), mgr_ptr_(mgr_ptr) {}

  std::atomic<uint64_t> thread_time_;
  void* mgr_ptr_;
};

/// @brief 记录线程进入RCU缓存的时间
class ThreadTimeMgr {
 public:
  ThreadTimeMgr();

  ~ThreadTimeMgr();

  void RcuEnter();

  void RcuExit();

  uint64_t MinTime();

 private:
  static void OnThreadExit(void* ptr);

 private:
  std::mutex lock_;
  std::set<ThreadTime*> thread_time_set_;

  pthread_key_t thread_time_key_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CACHE_RCU_TIME_H_
