//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use
//  this file
//  except in compliance with the License. You may obtain a copy of the License
//  at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the
//  specific
//  language governing permissions and limitations under the License.
//

#include "reactor/reactor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <pthread.h>
#include <sys/eventfd.h>

#include "polaris/model.h"
#include "reactor/event.h"

namespace polaris {

class TestEvent : public EventBase {
 public:
  explicit TestEvent(Reactor &reactor)
      : EventBase(eventfd(0, EFD_NONBLOCK)), reactor_(reactor), read_count_(0), write_count_(0) {}

  virtual ~TestEvent() { close(fd_); }

  virtual void ReadHandler() {
    eventfd_t data;
    eventfd_read(fd_, &data);
    read_count_ += data;
  }

  virtual void WriteHandler() { write_count_++; }

  virtual void CloseHandler() {}

  void Write(int data) { eventfd_write(fd_, data); }

 public:
  Reactor &reactor_;
  volatile int read_count_;
  volatile int write_count_;
  TimingTaskIter timeout_iter_;
};

class ReactorTest : public ::testing::Test {
 protected:
  virtual void SetUp() { tid_ = 0; }

  virtual void TearDown() {
    if (tid_ != 0) {
      pthread_join(tid_, nullptr);
      tid_ = 0;
    }
  }

 protected:
  pthread_t tid_;
  Reactor reactor_;

  static void *ThreadRun(void *args) {
    Reactor *reactor = static_cast<Reactor *>(args);
    reactor->Run();
    return nullptr;
  }
};

TEST_F(ReactorTest, EventHandler) {
  TestEvent *event = new TestEvent(reactor_);
  event->Write(20);
  ASSERT_TRUE(reactor_.AddEventHandler(event));  // 启动之前可以添加事件
  int rc = pthread_create(&tid_, nullptr, ThreadRun, &reactor_);
  ASSERT_TRUE(rc == 0 && tid_ > 0);
  while (event->write_count_ < 2) {
  }  // 等待触发事件：可写->可读->可写
  reactor_.Stop();
  ASSERT_EQ(event->read_count_, 20);  // 一定完成了读事件，读到数据
  delete event;                       // 需要自己释放
}

void AddEventTask(TestEvent *test_event) { test_event->reactor_.AddEventHandler(test_event); }

class DeleteEventTask : public Task {
 public:
  explicit DeleteEventTask(TestEvent *test_event) : event_(test_event) {}
  virtual ~DeleteEventTask() { delete event_; }

  virtual void Run() { event_->reactor_.RemoveEventHandler(event_->GetFd()); }

 private:
  TestEvent *event_;
};

TEST_F(ReactorTest, SubmitTask) {
  int rc = pthread_create(&tid_, nullptr, ThreadRun, &reactor_);
  ASSERT_TRUE(rc == 0 && tid_ > 0);
  TestEvent *event = new TestEvent(reactor_);
  // 已经运行的reactor不能直接添加事件，可以通过提交任务添加
  event->Write(50);
  reactor_.SubmitTask(new FuncTask<TestEvent>(AddEventTask, event));
  reactor_.Notify();
  while (event->write_count_ < 2) {
  }                                                 // 等待触发事件：可写->可读->可写
  ASSERT_EQ(event->read_count_, 50);                // 一定完成了读事件，读到数据
  reactor_.SubmitTask(new DeleteEventTask(event));  // 提交删除任务
  reactor_.Stop();
}

template <int N>
void WriteTask(TestEvent *event) {
  event->Write(N);
  EXPECT_TRUE(true) << N;
}

template <int N>
void SetupTimeoutWrite(TestEvent *event) {
  event->timeout_iter_ = event->reactor_.AddTimingTask(new TimingFuncTask<TestEvent>(WriteTask<N>, event, N));
}

void CancelTimeoutWrite(TestEvent *event) { event->reactor_.CancelTimingTask(event->timeout_iter_); }

TEST_F(ReactorTest, TimingTask) {
  TestEvent *event = new TestEvent(reactor_);
  ASSERT_TRUE(reactor_.AddEventHandler(event));  // 启动之前可以添加事件
  int rc = pthread_create(&tid_, nullptr, ThreadRun, &reactor_);
  ASSERT_TRUE(rc == 0 && tid_ > 0);

  reactor_.SubmitTask(new FuncTask<TestEvent>(SetupTimeoutWrite<10000>,
                                              event));  // 10s后写入10000
  reactor_.Notify();
  reactor_.SubmitTask(new FuncTask<TestEvent>(CancelTimeoutWrite, event));    // 取消1s后写入
  reactor_.SubmitTask(new FuncTask<TestEvent>(SetupTimeoutWrite<5>, event));  // 5ms后写入5
  reactor_.Notify();

  while (event->write_count_ < 2) {
  }  // 等待触发事件：可写->可读->可写
  reactor_.Stop();
  ASSERT_EQ(event->read_count_, 5);  // 一定完成了读事件，读到数据
  delete event;                      // 需要自己释放
}

TEST_F(ReactorTest, TestDeferDeleteTask) {
  reactor_.SubmitTask(new DeferDeleteTask<int>(new int()));
  reactor_.RunOnce();  // 执行释放int的任务

  TestEvent *event = new TestEvent(reactor_);  // reactor析构时会释放event
  reactor_.SubmitTask(new DeferDeleteTask<TestEvent>(event));
  reactor_.Stop();
}

class ServiceBaseTask : public ServiceBase {
 public:
  ServiceBaseTask() : count_(0) {}

  static void AddCount(ServiceBaseTask *task) { task->count_++; }

  int GetCount() { return count_; }

 private:
  int count_;
};

TEST_F(ReactorTest, TestServiceBaseTask) {
  ServiceBaseTask *task = new ServiceBaseTask();
  reactor_.SubmitTask(new FuncRefTask<ServiceBaseTask>(ServiceBaseTask::AddCount, task));
  reactor_.RunOnce();  // 执行释放int的任务
  ASSERT_EQ(task->GetCount(), 1);
  reactor_.Stop();
  task->DecrementRef();
}

void ThreadLocalReactorCheck(Reactor *reactor) {
  Reactor &thread_local_reactor = ThreadLocalReactor();
  ASSERT_EQ(&thread_local_reactor, reactor);
}

TEST_F(ReactorTest, TestThreadLocalReactor) {
  reactor_.SubmitTask(new FuncTask<Reactor>(ThreadLocalReactorCheck, &reactor_));
  reactor_.RunOnce();
  reactor_.Stop();
}

}  // namespace polaris
