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

#ifndef POLARIS_CPP_POLARIS_REACTOR_NOTIFY_H_
#define POLARIS_CPP_POLARIS_REACTOR_NOTIFY_H_

#include <features.h>

#include "reactor/event.h"

#if defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 9)
#  define HAVE_EVENTFD 1
#endif

namespace polaris {

// 通知事件，用于唤醒Reactor
// 优先使用eventfd实现
// eventfd不支持的情况下使用pipe实现
class Notifier : public EventBase {
 public:
  Notifier();
  virtual ~Notifier();

  virtual void ReadHandler();
  virtual void WriteHandler() {}
  virtual void CloseHandler() {}

  void Notify();  // 调用此方法可唤醒Reactor

 private:
  void Init();

#ifndef HAVE_EVENTFD
  int fd2_;  // pipe[1]
#endif
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_REACTOR_NOTIFY_H_
