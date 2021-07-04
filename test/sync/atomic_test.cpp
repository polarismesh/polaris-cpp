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

#include "sync/atomic.h"

#include <gtest/gtest.h>
#include <pthread.h>
#include <stdint.h>

#include <vector>

namespace polaris {

static const uint64_t DATA_UTIL = 10000000;

struct ThreadData {
  pthread_t tid_;
  uint64_t local_count_;
  sync::Atomic<uint64_t> *data_;
};

void *ThreadAdd(void *args) {
  ThreadData *thread_data      = static_cast<ThreadData *>(args);
  sync::Atomic<uint64_t> &data = *thread_data->data_;
  while (data.Load() < 1) {
  }
  while (data.Load() < DATA_UTIL) {
    thread_data->local_count_++;
    ++data;
  }
  return NULL;
}

void *ThreadSub(void *args) {
  ThreadData *thread_data      = static_cast<ThreadData *>(args);
  sync::Atomic<uint64_t> &data = *thread_data->data_;
  while (data.Load() >= DATA_UTIL) {
  }
  do {
    uint64_t value = data.Load();
    if (value == 0) {
      break;
    }
    if (data.Cas(value, value - 1)) {
      thread_data->local_count_++;
    }
  } while (true);
  return NULL;
}

TEST(AtomicTest, AddThenSub) {
  std::vector<ThreadData> threads(8);
  sync::Atomic<uint64_t> data;
  for (std::size_t i = 0; i < threads.size(); i++) {
    ThreadData &thread_data  = threads[i];
    thread_data.local_count_ = 0;
    thread_data.data_        = &data;
    pthread_create(&thread_data.tid_, NULL, ThreadAdd, &thread_data);
    ASSERT_TRUE(thread_data.tid_ > 0);
  }
  data.Cas(0, 1);  // 触发线程执行
  uint64_t local_sum = 0;
  for (std::size_t i = 0; i < threads.size(); i++) {
    pthread_join(threads[i].tid_, NULL);
    local_sum += threads[i].local_count_;
  }
  uint64_t total_sum = data.Load();
  ASSERT_GE(total_sum, DATA_UTIL);
  ASSERT_EQ(local_sum, total_sum - 1);

  for (std::size_t i = 0; i < threads.size(); i++) {
    ThreadData &thread_data  = threads[i];
    thread_data.local_count_ = 0;
    thread_data.data_        = &data;
    pthread_create(&thread_data.tid_, NULL, ThreadSub, &thread_data);
    ASSERT_TRUE(thread_data.tid_ > 0);
  }
  while (data.Load() > DATA_UTIL) {
    --data;
  }
  uint64_t old_data = data.Exchange(DATA_UTIL - 1);  // 触发线程执行
  ASSERT_EQ(old_data, DATA_UTIL);
  local_sum = 0;
  for (std::size_t i = 0; i < threads.size(); i++) {
    pthread_join(threads[i].tid_, NULL);
    local_sum += threads[i].local_count_;
  }
  ASSERT_EQ(data.Load(), 0);
  ASSERT_EQ(local_sum, DATA_UTIL - 1);
}

TEST(AtomicTest, OperatorTest) {
  sync::Atomic<int> data;
  ASSERT_EQ(data++, 0);
  ASSERT_EQ(++data, 2);
  ASSERT_EQ(data--, 2);
  ASSERT_EQ(--data, 0);

  data &= 123;
  ASSERT_EQ(data, 0);
  data |= 0xff;
  ASSERT_EQ(data, 0xff);
  data &= 0xf0;
  ASSERT_EQ(data, 0xf0);
  data ^= 0xfff;
  ASSERT_EQ(data, 0xf0f);
}

TEST(AtomicTest, OperatorTest2) {
  sync::Atomic<int> data;
  ASSERT_EQ(data += 2, 2);
  ASSERT_EQ(data -= 2, 0);
  ASSERT_EQ(data += 4, 4);
  ASSERT_EQ(data -= 3, 1);
}

}  // namespace polaris
