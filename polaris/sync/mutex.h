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

#ifndef POLARIS_CPP_POLARIS_SYNC_MUTEX_H_
#define POLARIS_CPP_POLARIS_SYNC_MUTEX_H_

#include <pthread.h>

#include "polaris/noncopyable.h"

namespace polaris {
namespace sync {

// 封装pthread mutex
class Mutex : Noncopyable {
public:
  Mutex();

  ~Mutex();

  void Lock();  // 加锁

  void Unlock();  // 释放锁

private:
  friend class CondVar;
  pthread_mutex_t mutex_;
};

// RAII方式封装Mutex的Lock和UnLock
class MutexGuard : Noncopyable {
public:
  explicit MutexGuard(Mutex& mutex);

  ~MutexGuard();

private:
  Mutex& mutex_;
};

}  // namespace sync
}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_SYNC_MUTEX_H_
