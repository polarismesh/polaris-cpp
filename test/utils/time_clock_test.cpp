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

extern int g_custom_clock_ref_count;
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
  int *id = static_cast<int *>(arg);
  uint64_t last_time = Time::GetSystemTimeMs();
  for (int i = 0; i < 100000000; ++i) {
    uint64_t current_time = Time::GetSystemTimeMs();
    EXPECT_LE(last_time, current_time) << i << "  " << *id;
    last_time = current_time;
  }
  Time::TryShutdomClock();
  delete id;
  return nullptr;
}

TEST_F(TimeClockTest, MultiThreadTest) {
  std::vector<pthread_t> thread_list;
  pthread_t tid;
  for (int i = 0; i < 8; ++i) {
    int *id = new int(i);
    pthread_create(&tid, nullptr, ThreadFunc, id);
    thread_list.push_back(tid);
  }
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], nullptr);
  }
  thread_list.clear();
}

TEST_F(TimeClockTest, ClockThreadWithCustomClockFunc) {
  TestUtils::SetUpFakeTime();  // 设置自定义时间函数
  Time::TrySetUpClock();
  ASSERT_EQ(g_custom_clock_update_tid, 0);  // 自定义时间函数时不会启动内部时间线程
  Time::TryShutdomClock();
  TestUtils::TearDownFakeTime();
}

uint64_t TestTimeFunc() { return 42; }

TEST_F(TimeClockTest, TimeWithCustomClockFunc) {
  uint64_t current_time = Time::GetCoarseSteadyTimeMs();
  ASSERT_GT(current_time, 42);
  Time::SetCustomTimeFunc(TestTimeFunc, TestTimeFunc);
  ASSERT_EQ(Time::GetCoarseSteadyTimeMs(), 42);
  usleep(1000);
  Time::SetDefaultTimeFunc();
  uint64_t new_current_time = Time::GetSystemTimeMs();
  ASSERT_GE(new_current_time, current_time);
}

}  // namespace polaris
