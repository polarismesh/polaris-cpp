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

namespace polaris {

namespace sync {

CondVarNotify::CondVarNotify() : notified_(false) {}

CondVarNotify::~CondVarNotify() {}

bool CondVarNotify::WaitFor(uint64_t timeout) {
  if (notified_) return true;  // 已经就绪直接返回
  return WaitUntil(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout));
}

bool CondVarNotify::WaitUntil(const std::chrono::steady_clock::time_point& time_point) {
  if (notified_) return true;  // 已经就绪直接返回
  std::unique_lock<std::mutex> mutex_guard(mutex_);
  cond_.wait_until(mutex_guard, time_point, [=] { return notified_; });
  return notified_;
}

void CondVarNotify::Notify() {
  {
    std::lock_guard<std::mutex> mutex_guard(mutex_);
    notified_ = true;
  }
  cond_.notify_one();
}

void CondVarNotify::NotifyAll() {
  {
    std::lock_guard<std::mutex> mutex_guard(mutex_);
    notified_ = true;
  }
  cond_.notify_all();
}

};  // namespace sync

}  // namespace polaris
