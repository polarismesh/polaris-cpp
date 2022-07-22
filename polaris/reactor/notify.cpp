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

#include "notify.h"

#ifdef HAVE_EVENTFD
#  include <sys/eventfd.h>
#endif

#include <unistd.h>

#include "logger.h"
#include "reactor/event.h"
#include "utils/netclient.h"

namespace polaris {

Notifier::Notifier() : EventBase(0) {
#ifdef HAVE_EVENTFD
  fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
#else
  Init();
#endif
}

Notifier::~Notifier() {
  if (fd_ >= 0) {
    close(fd_);
  }
  fd_ = -1;
#ifndef HAVE_EVENTFD
  if (fd2_ >= 0) {
    close(fd2_);
  }
  fd2_ = -1;
#endif
}

void Notifier::Init() {
#ifndef HAVE_EVENTFD
  int pipefd[2];
  int r = pipe(pipefd);
  POLARIS_ASSERT(r == 0);
  POLARIS_ASSERT(NetClient::SetNonBlock(pipefd[0]) == 0);
  POLARIS_ASSERT(NetClient::SetNonBlock(pipefd[1]) == 0);
  NetClient::SetCloExec(pipefd[0]);
  NetClient::SetCloExec(pipefd[1]);
  fd_ = pipefd[0];
  fd2_ = pipefd[1];
#endif
}

void Notifier::ReadHandler() {
#ifdef HAVE_EVENTFD
  eventfd_t tmp;
  eventfd_read(fd_, &tmp);
#else
  char buf[128];
  ssize_t r;
  while ((r = read(fd_, buf, sizeof(buf))) != 0) {
    if (r > 0) continue;
    switch (errno) {
      case EAGAIN:
        return;
      case EINTR:
        continue;
      default:
        return;
    }
  }
#endif
}

void Notifier::Notify() {
#ifdef HAVE_EVENTFD
  eventfd_write(fd_, 1);
#else
  char c = 0;
  while (write(fd_, &c, 1) != 1 && errno == EINTR) {
  }
#endif
}

}  // namespace polaris
