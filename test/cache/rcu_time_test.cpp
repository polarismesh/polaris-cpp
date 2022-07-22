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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <pthread.h>

#include <atomic>

#include "cache/rcu_time.h"
#include "test_utils.h"

namespace polaris {

class RcuTimeTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    TestUtils::SetUpFakeTime();
    thread_time_mgr_ = new ThreadTimeMgr();
    ASSERT_TRUE(thread_time_mgr_ != nullptr);
  }

  virtual void TearDown() {
    if (thread_time_mgr_ != nullptr) {
      delete thread_time_mgr_;
      thread_time_mgr_ = nullptr;
    }
    TestUtils::TearDownFakeTime();
  }

 protected:
  ThreadTimeMgr *thread_time_mgr_;
};

TEST_F(RcuTimeTest, SingleThreadTest) {
  // 没有线程进入过缓冲区
  ASSERT_EQ(thread_time_mgr_->MinTime(), Time::GetCoarseSteadyTimeMs());

  for (int i = 0; i < 100; ++i) {
    TestUtils::FakeNowIncrement(1000);
    thread_time_mgr_->RcuEnter();
    ASSERT_EQ(thread_time_mgr_->MinTime(), Time::GetCoarseSteadyTimeMs());
    thread_time_mgr_->RcuExit();
    TestUtils::FakeNowIncrement(1000);
    ASSERT_EQ(thread_time_mgr_->MinTime(), Time::GetCoarseSteadyTimeMs());
  }
}

void *ThreadFunc(void *args) {
  ThreadTimeMgr *thread_time_mgr = static_cast<ThreadTimeMgr *>(args);
  for (int i = 0; i < 100000; ++i) {
    TestUtils::FakeNowIncrement(1000);
    thread_time_mgr->RcuEnter();
    EXPECT_LE(thread_time_mgr->MinTime(), Time::GetCoarseSteadyTimeMs());
    thread_time_mgr->RcuExit();
  }
  return nullptr;
}

TEST_F(RcuTimeTest, MultiThreadTest) {
  std::vector<pthread_t> thread_list;
  pthread_t tid;
  for (int i = 0; i < 64; ++i) {
    pthread_create(&tid, nullptr, ThreadFunc, thread_time_mgr_);
    thread_list.push_back(tid);
  }
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], nullptr);
  }
  thread_list.clear();
  ASSERT_EQ(thread_time_mgr_->MinTime(), Time::GetCoarseSteadyTimeMs());
}

struct ThreadCount {
  explicit ThreadCount(ThreadTimeMgr *thread_time_mgr) : thread_time_mgr_(thread_time_mgr), count_(0) {}
  ThreadTimeMgr *thread_time_mgr_;
  std::atomic<int> count_;
};

void *ThreadFuncWithCount(void *args) {
  ThreadCount *thread_args = static_cast<ThreadCount *>(args);
  thread_args->thread_time_mgr_->RcuEnter();
  thread_args->thread_time_mgr_->RcuExit();
  thread_args->count_++;
  while (thread_args->count_ != 0) {
    usleep(10000);
  }
  return nullptr;
}

TEST_F(RcuTimeTest, TestTlsFree) {
  std::vector<pthread_t> thread_list;
  int thread_num = 64;
  pthread_t tid;
  ThreadCount thread_count(thread_time_mgr_);
  for (int i = 0; i < thread_num; ++i) {
    pthread_create(&tid, nullptr, ThreadFuncWithCount, &thread_count);
    thread_list.push_back(tid);
  }
  thread_time_mgr_->RcuEnter();
  thread_time_mgr_->RcuExit();
  while (thread_count.count_ != thread_num) {
    usleep(10000);
  }
  delete thread_time_mgr_;
  thread_time_mgr_ = nullptr;
  thread_count.count_ = 0;
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], nullptr);
  }
  thread_list.clear();
}

}  // namespace polaris
