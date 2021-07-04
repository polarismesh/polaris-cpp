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

#include <gtest/gtest.h>

#include <pthread.h>

#include "sync/cond_var.h"
#include "utils/time_clock.h"

namespace polaris {

namespace sync {

class CondVarNotifyTest : public ::testing::Test {
protected:
  virtual void SetUp() {}

  virtual void TearDown() {}
};

TEST_F(CondVarNotifyTest, SingleThreadTest1) {
  CondVarNotify notify1;
  ASSERT_FALSE(notify1.IsNotified());
  notify1.Notify();
  ASSERT_TRUE(notify1.IsNotified());
  ASSERT_TRUE(notify1.Wait(0));
}

TEST_F(CondVarNotifyTest, SingleThreadTest2) {
  CondVarNotify notify2;
  ASSERT_FALSE(notify2.IsNotified());
  notify2.NotifyAll();
  ASSERT_TRUE(notify2.IsNotified());
  timespec ts = Time::CurrentTimeAddWith(0);
  ASSERT_TRUE(notify2.Wait(ts));
}

struct CircuitNotify {
  CondVarNotify notify_in_;
  CondVarNotify notify_out_;
  int out_count_;
};

void *ThreadNotify(void *args) {
  CircuitNotify *data = static_cast<CircuitNotify *>(args);
  while (!data->notify_in_.IsNotified()) {
    data->notify_in_.Wait(1000);
  }
  {
    MutexGuard mutex_guard(data->notify_out_.GetMutex());
    data->out_count_++;
  }
  data->notify_out_.Notify();
  return NULL;
}

TEST_F(CondVarNotifyTest, MultiThreadTest) {
  std::vector<pthread_t> thread_list;
  pthread_t tid;
  CircuitNotify data;
  data.out_count_ = 0;
  for (int i = 0; i < 10; ++i) {
    pthread_create(&tid, NULL, ThreadNotify, &data);
    thread_list.push_back(tid);
  }
  data.notify_in_.NotifyAll();
  while (true) {
    data.notify_out_.Wait(1000);
    if (data.out_count_ == 10) {
      break;
    }
  }
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], NULL);
  }
  ASSERT_EQ(data.out_count_, 10);
}

}  // namespace sync
}  // namespace polaris
