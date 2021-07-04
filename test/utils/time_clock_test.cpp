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

#include "utils/time_clock.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <pthread.h>

#include "test_utils.h"

namespace polaris {

extern volatile int g_custom_clock_ref_count;
extern pthread_t g_custom_clock_update_tid;

class TimeClockTest : public ::testing::Test {
protected:
  virtual void SetUp() {
    ASSERT_EQ(g_custom_clock_ref_count, 0);
    ASSERT_EQ(g_custom_clock_update_tid, 0);
  }

  virtual void TearDown() {
    ASSERT_EQ(g_custom_clock_ref_count, 0);
    ASSERT_EQ(g_custom_clock_update_tid, 0);
  }

protected:
};

void *ThreadFunc(void *arg) {
  Time::TrySetUpClock();
  int *id            = static_cast<int *>(arg);
  uint64_t last_time = Time::GetCurrentTimeMs();
  for (int i = 0; i < 100000000; ++i) {
    uint64_t current_time = Time::GetCurrentTimeMs();
    EXPECT_LE(last_time, current_time) << i << "  " << *id;
    last_time = current_time;
  }
  Time::TryShutdomClock();
  delete id;
  return NULL;
}

TEST_F(TimeClockTest, MultiThreadTest) {
  std::vector<pthread_t> thread_list;
  pthread_t tid;
  for (int i = 0; i < 8; ++i) {
    int *id = new int(i);
    pthread_create(&tid, NULL, ThreadFunc, id);
    thread_list.push_back(tid);
  }
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], NULL);
  }
  thread_list.clear();
}

}  // namespace polaris
