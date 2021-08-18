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

#ifndef POLARIS_CPP_POLARIS_REACTOR_REACTOR_H_
#define POLARIS_CPP_POLARIS_REACTOR_REACTOR_H_

#include <pthread.h>
#include <stdint.h>

#include <map>
#include <vector>

#include "reactor/notify.h"
#include "reactor/task.h"
#include "sync/atomic.h"
#include "sync/mutex.h"

struct epoll_event;

namespace polaris {

enum ReactorStatus { kReactorInit, kReactorRun, kReactorStop };

class Reactor {
public:
  Reactor();
  ~Reactor();

  void Run() { Run(false); }  // 执行Event Loop

  void RunOnce() { Run(true); }  // for test，只执行一次事件循环

  // 线程不安全的方式注册和取消事件
  bool AddEventHandler(EventBase* event_handler);
  void RemoveEventHandler(int fd);

  // 线程不安全的方式增加定时任务和取消定时任务
  TimingTaskIter AddTimingTask(TimingTask* timing_task);
  void CancelTimingTask(TimingTaskIter iter);
  TimingTaskIter TimingTaskEnd() { return timing_tasks_.end(); }

  // 以下三个方法线程安全
  void SubmitTask(Task* task);           // 用于其他线程提交任务
  void Notify() { notifier_.Notify(); }  // 从epoll wait中唤醒Reactor
  void Stop();                           // 停止reactor

private:
  void RunPendingTask();  // 执行队列中的任务

  void RunTimingTask();  // 执行定时任务

  void RunEpollTask(uint64_t timeout);  // 执行读写任务

  uint64_t CalculateEpollWaitTime();  // 计算在epoll等待的时间

  void Run(bool once);  // 执行Event Loop，参数once控制是否只执行一次

private:
  int epoll_fd_;
  epoll_event* epoll_events_;
  pthread_t executor_tid_;  // 记录运行Reactor循环的线程，用于检查线程不安全方法的调用
  sync::Atomic<ReactorStatus> status_;
  Notifier notifier_;
  std::map<int, EventBase*> fd_holder_;  // 记录fd对应的event handler

  sync::Mutex queue_mutex_;           // 立刻执行的任务由别的线程提交，必须加锁
  std::vector<Task*> pending_tasks_;  // 立刻执行的任务

  std::multimap<uint64_t, TimingTask*> timing_tasks_;  // 定时执行的任务
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_REACTOR_REACTOR_H_
