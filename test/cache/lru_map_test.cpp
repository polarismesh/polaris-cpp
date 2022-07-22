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

#include <memory>

#include "cache/lru_map.h"
#include "cache/rcu_time.h"

#include "test_utils.h"

namespace polaris {

class LruMapTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    capacity_ = 10;
    lru_map_.reset(new LruHashMap<int, int>(capacity_, MurmurInt32, LruValueNoOp, LruValueDelete));
  }

  virtual void TearDown() {}

 protected:
  int capacity_;
  std::unique_ptr<LruHashMap<int, int> > lru_map_;
};

TEST_F(LruMapTest, SingleThreadTest) {
  for (int i = 0; i < 5000; i++) {
    int* data = new int(i + 1);
    lru_map_->Update(i, data);
    if (i >= capacity_) {
      int* data = lru_map_->Get(i - capacity_);
      ASSERT_TRUE(data == nullptr) << i;
    }
  }
  for (int i = 0; i < 5000 - capacity_; i++) {
    int* data = lru_map_->Get(i);
    ASSERT_TRUE(data == nullptr) << i;
  }
  for (int i = 5000 - capacity_; i < 5000; i++) {
    int* data = lru_map_->Get(i);
    ASSERT_EQ(*data, i + 1);
  }
}

TEST_F(LruMapTest, SingleThreadTest2) {
  for (int i = 0; i < 10000000; i++) {
    int key = rand() % 10000;
    int op = rand() % 3;
    if (op == 0) {
      int* value = new int(key);
      lru_map_->Update(key, value);
    } else {
      lru_map_->Get(key);
    }
  }
}

void* QueueThreadFunc(void* args) {
  LruQueue<int>* lru_queue = static_cast<LruQueue<int>*>(args);
  for (int i = 0; i < 1000000; ++i) {
    lru_queue->Enqueue(new int(i));
  }
  return nullptr;
}

TEST_F(LruMapTest, MultiLruQueue) {
  std::vector<pthread_t> thread_list;
  pthread_t tid;
  LruQueue<int> lru_queue;
  for (int i = 0; i < 10; ++i) {
    pthread_create(&tid, nullptr, QueueThreadFunc, &lru_queue);
    thread_list.push_back(tid);
  }
  int count = 0;
  while (count < 10000000) {
    if (lru_queue.Dequeue(Time::GetCoarseSteadyTimeMs())) {
      count++;
    }
  }
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], nullptr);
  }
  thread_list.clear();
}

struct MapThreadArgs {
  LruHashMap<int, int>* lru_map_;
  ThreadTimeMgr* thread_time_mgr_;
  int index_;
  std::atomic<bool> stop_;
  pthread_t tid_;
};

void* MapThreadFunc(void* args) {
  MapThreadArgs* thread_args = static_cast<MapThreadArgs*>(args);
  LruHashMap<int, int>* lru_map = thread_args->lru_map_;
  while (!thread_args->stop_.load()) {
    int key = rand() % 10000;
    int op = rand() % 3;
    thread_args->thread_time_mgr_->RcuEnter();
    if (op == 0) {
      int* value = new int(key);
      lru_map->Update(key, value);
    } else {
      lru_map->Get(key);
    }
    thread_args->thread_time_mgr_->RcuExit();
  }
  return nullptr;
}

TEST_F(LruMapTest, MultiLruMap) {
  ThreadTimeMgr* thread_time_mgr = new ThreadTimeMgr();
  const int thread_size = 4;
  MapThreadArgs thread_list[thread_size];
  pthread_t tid;
  for (int i = 0; i < thread_size; ++i) {
    MapThreadArgs* thread_args = &thread_list[i];
    thread_args->index_ = i;
    thread_args->lru_map_ = lru_map_.get();
    thread_args->thread_time_mgr_ = thread_time_mgr;
    thread_args->stop_ = false;
    pthread_create(&tid, nullptr, MapThreadFunc, thread_args);
    thread_args->tid_ = tid;
  }
  sleep(5);
  for (int i = thread_size - 1; i >= 0; --i) {
    MapThreadArgs* thread_args = &thread_list[i];
    thread_args->stop_.store(true);
    pthread_join(thread_args->tid_, nullptr);
  }
  delete thread_time_mgr;
}

}  // namespace polaris
