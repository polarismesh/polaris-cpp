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

#include "sync/mutex.h"

namespace polaris {

namespace sync {

struct CountData {
  Mutex mutex_;
  int count_;
};

class MutexTest : public ::testing::Test {
protected:
  virtual void SetUp() { count_data_.count_ = 0; }

  virtual void TearDown() {}

protected:
  CountData count_data_;
};

static const int kCountTime = 10000;

void *ThreadCountWithMutex(void *args) {
  CountData *count_data = static_cast<CountData *>(args);
  for (int i = 0; i < kCountTime; ++i) {
    if (i % 2 == 0) {
      count_data->mutex_.Lock();
      count_data->count_++;
      count_data->mutex_.Unlock();
    } else {
      MutexGuard guard(count_data->mutex_);
      count_data->count_++;
    }
  }
  return NULL;
}

TEST_F(MutexTest, SingleThreadTest) {
  ThreadCountWithMutex(&count_data_);
  ASSERT_EQ(count_data_.count_, kCountTime);
}

TEST_F(MutexTest, MultiThreadTest) {
  std::vector<pthread_t> thread_list;
  pthread_t tid;
  for (int i = 0; i < 10; ++i) {
    pthread_create(&tid, NULL, ThreadCountWithMutex, &count_data_);
    thread_list.push_back(tid);
  }
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], NULL);
  }
  ASSERT_EQ(count_data_.count_, 10 * kCountTime);
}

}  // namespace sync
}  // namespace polaris
