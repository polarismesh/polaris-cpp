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

#include "reactor/reactor.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <iosfwd>

#include <utility>

#include "logger.h"
#include "reactor/event.h"
#include "reactor/notify.h"
#include "utils/time_clock.h"

namespace polaris {

static const int kEpollEventSize           = 1024;
static const uint64_t kEpollTimeoutDefault = 10;

Reactor::Reactor() {
  epoll_fd_     = epoll_create(kEpollEventSize);
  epoll_events_ = new epoll_event[kEpollEventSize];
  POLARIS_ASSERT(epoll_fd_ >= 0 && "reactor create epoll failed!");
  stopped_      = true;
  executor_tid_ = 0;
  AddEventHandler(&notifier_);
}

Reactor::~Reactor() {
  POLARIS_ASSERT(stopped_ == true);
  RemoveEventHandler(notifier_.GetFd());

  // 这里必须先删除timeout，因为有些定时任务会用于检查请求超时
  // 超时后删除请求对象，请求对象提交异步删除链接到pending_tasks中
  for (TimingTaskIter it = timing_tasks_.begin(); it != timing_tasks_.end(); ++it) {
    delete it->second;
  }
  for (std::size_t i = 0; i < pending_tasks_.size(); ++i) {
    delete pending_tasks_[i];
  }

  // EventBase对象外部删除
  fd_holder_.clear();
  close(epoll_fd_);
  delete[] epoll_events_;
}

bool Reactor::AddEventHandler(EventBase* event_handler) {
  POLARIS_ASSERT(executor_tid_ == 0 || executor_tid_ == pthread_self());
  int fd = event_handler->GetFd();
  epoll_event epoll_event;
  epoll_event.data.ptr = reinterpret_cast<void*>(event_handler);
  epoll_event.events   = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLERR | EPOLLRDHUP;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &epoll_event) < 0) {
    POLARIS_LOG(LOG_ERROR, "epoll add fd:%d with errno:%d", fd, errno);
    close(fd);
    return false;
  }
  fd_holder_[fd] = event_handler;
  return true;
}

void Reactor::RemoveEventHandler(int fd) {
  POLARIS_ASSERT(executor_tid_ == 0 || executor_tid_ == pthread_self());
  if (fd_holder_.count(fd)) {
    epoll_event epoll_event;
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &epoll_event);
    fd_holder_.erase(fd);
  }
}

TimingTaskIter Reactor::AddTimingTask(TimingTask* timing_task) {
  POLARIS_ASSERT(executor_tid_ == 0 || executor_tid_ == pthread_self());
  uint64_t expiration = Time::GetCurrentTimeMs() + timing_task->GetInterval();
  return timing_tasks_.insert(std::make_pair(expiration, timing_task));
}

void Reactor::CancelTimingTask(TimingTaskIter iter) {
  POLARIS_ASSERT(executor_tid_ == 0 || executor_tid_ == pthread_self());
  delete iter->second;
  timing_tasks_.erase(iter);
}

void Reactor::SubmitTask(Task* task) {
  sync::MutexGuard mutex_guard(queue_mutex_);
  pending_tasks_.push_back(task);
}

void Reactor::Stop() {
  stopped_ = true;
  notifier_.Notify();
}

void Reactor::RunPendingTask() {
  std::vector<Task*> pending_tasks;
  do {
    sync::MutexGuard mutex_guard(queue_mutex_);
    pending_tasks.swap(pending_tasks_);
  } while (false);

  for (std::size_t i = 0; i < pending_tasks.size(); ++i) {
    Task*& task = pending_tasks[i];
    task->Run();
    delete task;

    if (i % 100 == 0) {
      RunEpollTask(0);
    }
  }
}

void Reactor::RunTimingTask() {
  while (!timing_tasks_.empty()) {
    TimingTaskIter it = timing_tasks_.begin();
    if (it->first > Time::GetCurrentTimeMs()) {
      return;  // 剩余任务都没有到执行时间
    }

    TimingTask* timing_task = it->second;
    timing_tasks_.erase(it);
    timing_task->Run();

    uint64_t next_run_time = timing_task->NextRunTime();
    if (next_run_time > 0) {
      timing_tasks_.insert(std::make_pair(next_run_time, timing_task));
    } else {
      delete timing_task;  // 不用在执行
    }
  }
}

uint64_t Reactor::CalculateEpollWaitTime() {
  if (timing_tasks_.empty()) {
    return kEpollTimeoutDefault;
  }
  // 查找最新需要执行的任务时间来决定epoll等待的时间
  uint64_t expire_time  = timing_tasks_.begin()->first;
  uint64_t current_time = Time::GetCurrentTimeMs();
  if (expire_time > current_time) {
    uint64_t diff = expire_time - current_time;
    return diff < kEpollTimeoutDefault ? diff : kEpollTimeoutDefault;
  }
  return 0;
}

void Reactor::RunEpollTask(uint64_t timeout) {
  int ret = epoll_wait(epoll_fd_, epoll_events_, kEpollEventSize, timeout);
  for (int i = 0; i < ret; i++) {
    EventBase* event = reinterpret_cast<EventBase*>(epoll_events_[i].data.ptr);
    if (epoll_events_[i].events & EPOLLIN) {
      event->ReadHandler();
    }
    if (epoll_events_[i].events & EPOLLOUT) {
      event->WriteHandler();
    }
    if (epoll_events_[i].events & EPOLLRDHUP || epoll_events_[i].events & EPOLLERR) {
      event->CloseHandler();
    }
  }
}

void Reactor::Run(bool once) {
  stopped_      = false;
  executor_tid_ = pthread_self();

  // 屏蔽线程的pipe broken singal
  sigset_t signal_mask;
  sigemptyset(&signal_mask);
  sigaddset(&signal_mask, SIGPIPE);
  int rc = pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);
  POLARIS_ASSERT(rc == 0)

  while (!stopped_) {
    RunPendingTask();

    RunEpollTask(CalculateEpollWaitTime());

    RunTimingTask();
    if (once) {
      break;
    }
  }
  executor_tid_ = 0;
}

}  // namespace polaris
