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

#include "cache/rcu_map.h"
#include "cache/rcu_time.h"

#include "test_utils.h"

#include "polaris/model.h"

namespace polaris {

class ServiceValue : public ServiceBase {
public:
  explicit ServiceValue(int value) { value_ = value; }

  virtual ~ServiceValue() {}

  int GetValue() { return value_; }

private:
  int value_;
};

class RcuMapTest : public ::testing::Test {
protected:
  virtual void SetUp() { rcu_map_ = new RcuMap<int, ServiceValue>(); }

  virtual void TearDown() {
    if (rcu_map_ != NULL) {
      delete rcu_map_;
      rcu_map_ = NULL;
    }
  }

protected:
  RcuMap<int, ServiceValue> *rcu_map_;
};

TEST_F(RcuMapTest, SingleThreadTest) {
  ServiceValue *value = rcu_map_->Get(0);
  ASSERT_TRUE(value == NULL);

  std::vector<int *> values_need_delete;
  for (int i = 0; i < 100; ++i) {
    rcu_map_->Update(i, new ServiceValue(i));
    for (int j = 0; j < i; j++) {
      if (j % 2 == 0) {
        rcu_map_->Delete(i);
        ASSERT_TRUE(rcu_map_->Get(i) == NULL);
        rcu_map_->Update(i, new ServiceValue(j));
      } else {
        rcu_map_->Update(i, new ServiceValue(i - 1));
      }
      value = rcu_map_->Get(i);
      ASSERT_TRUE(value != NULL);
      if (j % 2 == 0) {
        ASSERT_EQ(value->GetValue(), j);
      } else {
        ASSERT_EQ(value->GetValue(), i - 1);
      }
      value->DecrementRef();
    }
    rcu_map_->CheckGc(Time::GetCurrentTimeMs());
  }
}

struct ThreadArgs {
  RcuMap<int, ServiceValue> *cache_;
  ThreadTimeMgr *thread_time_mgr_;
};

void *RandomOperationCache(void *args) {
  ThreadArgs *thread_args                         = static_cast<ThreadArgs *>(args);
  int cache_num                                   = 100;
  int total                                       = cache_num * 5000;
  static __thread bool thread_local_seed_not_init = true;
  static __thread unsigned int thread_local_seed  = 0;
  if (thread_local_seed_not_init) {
    thread_local_seed_not_init = false;
    thread_local_seed          = time(NULL) ^ pthread_self();
  }
  for (int i = 0; i < total; ++i) {
    int key = i % cache_num;
    int op  = rand_r(&thread_local_seed) % 6;
    if (op == 0 || op == 2 || op == 4) {
      thread_args->thread_time_mgr_->RcuEnter();
      ServiceValue *value = thread_args->cache_->Get(key);
      if (value != NULL) {
        EXPECT_EQ((value->GetValue()) % cache_num, key) << key << ":" << value->GetValue();
        value->DecrementRef();
      }
      thread_args->thread_time_mgr_->RcuExit();
    } else if (op == 1 || op == 3) {
      thread_args->cache_->Update(key, new ServiceValue(i));
    } else {
      thread_args->cache_->Delete(key);
    }
    if (key == 0) {
      thread_args->cache_->CheckGc(thread_args->thread_time_mgr_->MinTime());
    }
  }
  return NULL;
}

TEST_F(RcuMapTest, MultiThreadTest) {
  ThreadTimeMgr *thread_time_mgr = new ThreadTimeMgr();
  ASSERT_TRUE(thread_time_mgr != NULL);
  std::vector<pthread_t> thread_list;
  ThreadArgs thread_args = {rcu_map_, thread_time_mgr};
  pthread_t tid;
  for (int i = 0; i < 32; ++i) {
    pthread_create(&tid, NULL, RandomOperationCache, &thread_args);
    thread_list.push_back(tid);
  }
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], NULL);
  }

  delete thread_time_mgr;
}

class RcuMapNoOpTest : public ::testing::Test {
protected:
  virtual void SetUp() { rcu_map_ = new RcuMap<int, int>(ValueNoOp, ValueNoOp); }

  virtual void TearDown() {
    if (rcu_map_ != NULL) {
      delete rcu_map_;
      rcu_map_ = NULL;
    }
  }

protected:
  RcuMap<int, int> *rcu_map_;
};

TEST_F(RcuMapNoOpTest, PutIfAbsentTest) {
  for (int i = 0; i < 1000; ++i) {
    int key        = i;
    int *value     = new int(i);
    int *old_value = rcu_map_->PutIfAbsent(key, value);
    ASSERT_TRUE(old_value == NULL);
    old_value = rcu_map_->PutIfAbsent(key, value);
    ASSERT_FALSE(old_value == NULL);
    rcu_map_->Delete(key);
    old_value = rcu_map_->PutIfAbsent(key, value);
    ASSERT_TRUE(old_value == NULL);
  }

  std::vector<int *> values;
  rcu_map_->GetAllValuesWithRef(values);
  for (size_t i = 0; i < values.size(); ++i) {
    if (values[i] != NULL) {
      delete values[i];
      values[i] = NULL;
    }
  }
}

}  // namespace polaris
