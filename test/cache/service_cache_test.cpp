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

#include "cache/service_cache.h"
#include "test_context.h"
#include "test_utils.h"

#include "polaris/model.h"

namespace polaris {

struct TestServiceCacheKey {
  int index_;
  ServiceBase *service_base_;

  bool operator<(const TestServiceCacheKey &rhs) const {
    if (this->service_base_ < rhs.service_base_) {
      return true;
    } else if (this->service_base_ == rhs.service_base_) {
      return this->index_ < rhs.index_;
    } else {
      return false;
    }
  }
};

class TestServiceCacheValue : public CacheValueBase {
public:
  virtual ~TestServiceCacheValue() {}
  int value_;
  ServiceBase *service_base_;
};

class ServiceCacheTest : public ::testing::Test {
protected:
  virtual void SetUp() { cache_ = new ServiceCache<TestServiceCacheKey>(); }

  virtual void TearDown() {
    for (std::size_t i = 0; i < thread_list_.size(); ++i) {
      pthread_join(thread_list_[i], NULL);
    }
    thread_list_.clear();
    cache_->DecrementRef();
  }

protected:
  ServiceCache<TestServiceCacheKey> *cache_;
  std::vector<pthread_t> thread_list_;

  static void *UpdateCache(void *args) {
    ServiceCache<TestServiceCacheKey> *cache =
        static_cast<ServiceCache<TestServiceCacheKey> *>(args);
    int cache_num = 100;
    int total     = cache_num * cache_num;
    for (int i = 0; i < total; ++i) {
      TestServiceCacheKey key      = {i % cache_num, NULL};
      TestServiceCacheValue *value = new TestServiceCacheValue();
      value->value_                = i % cache_num;
      value->service_base_         = NULL;
      cache->PutWithRef(key, value);
      value->DecrementRef();
    }
    return NULL;
  }
};

TEST_F(ServiceCacheTest, MultiThreadUpdate) {
  pthread_t tid;
  for (int i = 0; i < 8; ++i) {
    pthread_create(&tid, NULL, UpdateCache, cache_);
    thread_list_.push_back(tid);
  }
}

TEST_F(ServiceCacheTest, TestCacheClear) {
  Context *context          = TestContext::CreateContext();
  ContextImpl *context_impl = context->GetContextImpl();
  Time::TryShutdomClock();
  TestUtils::SetUpFakeTime();

  context_impl->RegisterCache(cache_);
  TestServiceCacheKey key      = {0, NULL};
  TestServiceCacheValue *value = new TestServiceCacheValue();
  cache_->PutWithRef(key, value);
  context_impl->ClearCache();

  // 刚加入，不会清除
  ServiceBase *got_value = cache_->GetWithRef(key);
  ASSERT_TRUE(got_value == value);
  got_value->DecrementRef();

  TestUtils::FakeNowIncrement(context_impl->GetCacheClearTime() - 1);
  context_impl->ClearCache();

  // 未达到清除时间，不会清除
  got_value = cache_->GetWithRef(key);
  ASSERT_TRUE(got_value == value);
  got_value->DecrementRef();

  TestUtils::FakeNowIncrement(context_impl->GetCacheClearTime());
  context_impl->ClearCache();
  // 达到清除时间，清除
  got_value = cache_->GetWithRef(key);
  ASSERT_TRUE(got_value == NULL);

  value->DecrementRef();
  TestUtils::TearDownFakeTime();
  delete context;
}

}  // namespace polaris
