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

#include "mutex.h"

#include <stddef.h>

namespace polaris {

namespace sync {

Mutex::Mutex() { pthread_mutex_init(&mutex_, NULL); }

Mutex::~Mutex() { pthread_mutex_destroy(&mutex_); }

void Mutex::Lock() { pthread_mutex_lock(&mutex_); }

void Mutex::Unlock() { pthread_mutex_unlock(&mutex_); }

MutexGuard::MutexGuard(Mutex& mutex) : mutex_(mutex) { mutex_.Lock(); }

MutexGuard::~MutexGuard() { mutex_.Unlock(); }

};  // namespace sync

}  // namespace polaris
