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

#ifndef POLARIS_CPP_POLARIS_REACTOR_EVENT_H_
#define POLARIS_CPP_POLARIS_REACTOR_EVENT_H_

namespace polaris {

// 事件基类
class EventBase {
 public:
  explicit EventBase(int fd) : fd_(fd) {}

  virtual ~EventBase() {}

  int GetFd() { return fd_; }

  // EPOLLIN 事件处理
  virtual void ReadHandler() = 0;

  // EPOLLOUT 事件处理
  virtual void WriteHandler() = 0;

  // EPOLLRDHUP EPOLLERR 事件处理
  virtual void CloseHandler() = 0;

 protected:
  int fd_;  // 事件发生的fd
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_REACTOR_EVENT_H_
