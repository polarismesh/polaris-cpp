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

struct CondVarData {
  CondVar cond_var_;
  Mutex mutex_;
  int count_;
};

class CondVarTest : public ::testing::Test {
protected:
  virtual void SetUp() { data_.count_ = 0; }

  virtual void TearDown() {}

protected:
  CondVarData data_;
};

void *ThreadSingal(void *args) {
  CondVarData *data = static_cast<CondVarData *>(args);
  MutexGuard mutex_guard(data->mutex_);
  data->count_++;
  data->cond_var_.Signal();
  return NULL;
}

TEST_F(CondVarTest, MultiThreadTest) {
  std::vector<pthread_t> thread_list;
  pthread_t tid;
  data_.mutex_.Lock();  // 先上锁，让新创建的所有线程等待
  for (int i = 0; i < 10; ++i) {
    pthread_create(&tid, NULL, ThreadSingal, &data_);
    thread_list.push_back(tid);
  }
  data_.mutex_.Unlock();
  while (data_.count_ < 10) {
    timespec ts = Time::CurrentTimeAddWith(1000);
    data_.cond_var_.Wait(data_.mutex_, ts);
  }
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], NULL);
  }
  ASSERT_EQ(data_.count_, 10);
}

}  // namespace sync
}  // namespace polaris
