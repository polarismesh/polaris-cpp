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

#include <list>
#include <map>
#include <mutex>

#include "polaris/noncopyable.h"
#include "reactor/notify.h"
#include "reactor/task.h"

struct epoll_event;

namespace polaris {

/// @brief Reactor 相当于一个消息循环，用于指定线程的处理事件
///
/// 每个线程创建一个Reactor，Reactor执行后会与当前线程绑定
/// 其他线程使用`SubmitTask`提交任务
class Reactor : Noncopyable {
 public:
  Reactor();
  ~Reactor();

  // 执行消息循环 直到收到退出信号
  void Run();

  // 注册fd事件监听，线程不安全
  bool AddEventHandler(EventBase* event_handler);
  // 取消fd事件监听，线程不安全
  void RemoveEventHandler(int fd);

  // 线程不安全的方式增加定时任务和取消定时任务
  TimingTaskIter AddTimingTask(TimingTask* timing_task);
  void CancelTimingTask(TimingTaskIter& iter);
  inline TimingTaskIter TimingTaskEnd() { return timing_tasks_.end(); }

  // 以下三个方法线程安全
  void SubmitTask(Task* task);                  // 用于其他线程提交任务
  inline void Notify() { notifier_.Notify(); }  // 从epoll wait中唤醒Reactor
  void Stop();                                  // 停止reactor

  /// @warning Just for testing: 只执行一次事件循环
  void RunOnce();

 private:
  // 执行队列中的任务
  void RunPendingTask();

  // 执行定时任务
  void RunTimingTask();

  // 执行epoll循环，触发读写回调
  void RunEpollTask(uint64_t timeout);

  // 计算在epoll等待的时间
  uint64_t CalculateEpollWaitTime();

 private:
  // 记录运行Reactor的线程ID，用于检查线程不安全方法的调用是在本线程调用
  pthread_t executor_tid_;

  // 用于其他线程通知退出消息循环
  volatile bool stop_received_;

  // epoll 相关变量
  int epoll_fd_;
  epoll_event* epoll_events_;

  // 用于其他线程通知唤醒epoll
  Notifier notifier_;

  // 记录fd对应的event handler
  std::map<int, EventBase*> fd_holder_;

  // 任务队列
  std::list<Task*> pending_tasks_;
  // 任务可由其他线程提交，加锁保护该队列
  std::mutex queue_mutex_;

  // 定时执行的map，key未执行时间
  std::multimap<uint64_t, TimingTask*> timing_tasks_;
};

// 获取当前线程的Reactor
Reactor& ThreadLocalReactor();

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_REACTOR_REACTOR_H_
