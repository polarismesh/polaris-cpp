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

#include "cache/rcu_time.h"
#include "cache/rcu_unordered_map.h"

#include "test_utils.h"

#include "polaris/model.h"

namespace polaris {

class RcuUnorderedMapTest : public ::testing::Test {
 protected:
  virtual void SetUp() {}

  virtual void TearDown() {}

 protected:
  RcuUnorderedMap<int, int> rcu_unordered_map_;
};

TEST_F(RcuUnorderedMapTest, SingleThreadTest) {
  std::shared_ptr<int> value = rcu_unordered_map_.Get(0);
  ASSERT_TRUE(value == nullptr);

  for (int i = 0; i < 100; ++i) {
    value.reset(new int(i));
    rcu_unordered_map_.Update(i, value);
    for (int j = 0; j < i; j++) {
      if (j % 2 == 0) {
        std::vector<int> delete_keys;
        delete_keys.push_back(i);
        rcu_unordered_map_.Delete(delete_keys);
        ASSERT_TRUE(rcu_unordered_map_.Get(i) == nullptr);
        value.reset(new int(j));
        rcu_unordered_map_.Update(i, value);
      } else {
        value.reset(new int(i - 1));
        rcu_unordered_map_.Update(i, value);
      }
      value = rcu_unordered_map_.Get(i);
      ASSERT_TRUE(value != nullptr) << i;
      if (j % 2 == 0) {
        ASSERT_EQ(*value, j);
      } else {
        ASSERT_EQ(*value, i - 1);
      }
    }
    rcu_unordered_map_.CheckGc(Time::GetCoarseSteadyTimeMs());
  }
}

struct ThreadArgs {
  RcuUnorderedMap<int, int> *cache_;
  ThreadTimeMgr *thread_time_mgr_;
};

void *RandomOperationCache(void *args) {
  ThreadArgs *thread_args = static_cast<ThreadArgs *>(args);
  int cache_num = 100;
  int total = cache_num * 5000;
  unsigned int thread_local_seed = time(nullptr) ^ pthread_self();
  for (int i = 0; i < total; ++i) {
    int key = i % cache_num;
    int op = rand_r(&thread_local_seed) % 6;
    if (op == 0 || op == 2 || op == 4) {
      thread_args->thread_time_mgr_->RcuEnter();
      std::shared_ptr<int> value = thread_args->cache_->Get(key);
      if (value != nullptr) {
        EXPECT_EQ((*value) % cache_num, key) << key << ":" << *value;
      }
      thread_args->thread_time_mgr_->RcuExit();
    } else if (op == 1 || op == 3) {
      thread_args->cache_->Update(key, std::shared_ptr<int>(new int(i)));
    } else {
      thread_args->cache_->Delete({key});
    }
    if (key % 10 == 0) {
      thread_args->cache_->CheckGc(thread_args->thread_time_mgr_->MinTime());
    }
  }
  return nullptr;
}

TEST_F(RcuUnorderedMapTest, MultiThreadTest) {
  ThreadTimeMgr *thread_time_mgr = new ThreadTimeMgr();
  ASSERT_TRUE(thread_time_mgr != nullptr);
  std::vector<pthread_t> thread_list;
  ThreadArgs thread_args = {&rcu_unordered_map_, thread_time_mgr};
  pthread_t tid;
  for (int i = 0; i < 32; ++i) {
    pthread_create(&tid, nullptr, RandomOperationCache, &thread_args);
    thread_list.push_back(tid);
  }
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], nullptr);
  }

  delete thread_time_mgr;
}

TEST_F(RcuUnorderedMapTest, PutIfAbsentTest) {
  for (int i = 0; i < 1000; ++i) {
    int key = i;
    std::shared_ptr<int> value = rcu_unordered_map_.CreateOrGet(key, [=] { return std::shared_ptr<int>(new int(i)); });
    ASSERT_TRUE(value != nullptr);
    ASSERT_EQ(*value, i);
    value = rcu_unordered_map_.CreateOrGet(key, [=] { return std::shared_ptr<int>(new int(i + 1)); });
    ASSERT_TRUE(value != nullptr);
    ASSERT_EQ(*value, i);
    rcu_unordered_map_.Delete(std::vector<int>({i}));
    value = rcu_unordered_map_.CreateOrGet(key, [=] { return std::shared_ptr<int>(new int(i + 2)); });
    ASSERT_TRUE(value != nullptr);
    ASSERT_EQ(*value, i + 2);
  }

  std::vector<std::shared_ptr<int>> values;
  rcu_unordered_map_.GetAllValues(values);
  for (size_t i = 0; i < values.size(); ++i) {
    if (values[i] != nullptr) {
      values[i] = nullptr;
    }
  }
}

TEST_F(RcuUnorderedMapTest, TestUpdatePredicate) {
  for (int i = 0; i < 1000; ++i) {
    int key = i;
    std::shared_ptr<int> value = rcu_unordered_map_.Update(
        key, [=](const std::shared_ptr<int> &) { return std::shared_ptr<int>(new int(i)); },
        [=](const std::shared_ptr<int> &) { return false; });
    ASSERT_TRUE(value != nullptr);
    ASSERT_EQ(*value, i);  // key不存在时不会管predicate
    value = rcu_unordered_map_.Update(
        key, [=](const std::shared_ptr<int> &old_value) { return std::shared_ptr<int>(new int(*old_value + 1)); },
        [=](const std::shared_ptr<int> &old_value) { return *old_value == i; });
    ASSERT_TRUE(value != nullptr);
    ASSERT_EQ(*value, i + 1);  // 会更新
    value = rcu_unordered_map_.Update(
        key, [=](const std::shared_ptr<int> &) { return std::shared_ptr<int>(new int(i)); },
        [=](const std::shared_ptr<int> &old_value) { return *old_value == i; });
    ASSERT_TRUE(value != nullptr);
    ASSERT_EQ(*value, i + 1);  // 不会更新
    rcu_unordered_map_.Delete(std::vector<int>({i}));
    value = rcu_unordered_map_.Update(
        key,
        [=](const std::shared_ptr<int> &old_value) {
          if (old_value != nullptr) {
            return std::shared_ptr<int>(new int(*old_value + 1));
          } else {
            return std::shared_ptr<int>(new int(i));
          }
        },
        [=](const std::shared_ptr<int> &old_value) { return *old_value == i; });
    ASSERT_TRUE(value != nullptr);
    ASSERT_EQ(*value, i);  // 会更新
  }

  std::vector<std::shared_ptr<int>> values;
  rcu_unordered_map_.GetAllValues(values);
  for (size_t i = 0; i < values.size(); ++i) {
    if (values[i] != nullptr) {
      values[i] = nullptr;
    }
  }
}

}  // namespace polaris
