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

#include "sync/future.h"

#include <gtest/gtest.h>
#include <pthread.h>

namespace polaris {

TEST(FutureTest, OnlyPromise) {
  {
    Promise<int> promise;
    ASSERT_FALSE(promise.IsReady());
    ASSERT_FALSE(promise.IsFailed());
    promise.SetError(kReturnResourceNotFound);
    ASSERT_TRUE(promise.IsReady());
    ASSERT_TRUE(promise.IsFailed());
  }
  {
    Promise<bool> promise;
    ASSERT_FALSE(promise.IsReady());
    ASSERT_FALSE(promise.IsFailed());
    promise.SetValue(new bool(false));
    ASSERT_TRUE(promise.IsReady());
    ASSERT_FALSE(promise.IsFailed());
  }
}

TEST(FutureTest, PromiseWithFuture) {
  Future<uint32_t> *future = nullptr;
  {
    Promise<uint32_t> promise;
    future = promise.GetFuture();
    ASSERT_FALSE(future->IsReady());
    ASSERT_FALSE(future->IsFailed());
    promise.SetError(kReturnNotInit);
    ASSERT_TRUE(future->IsReady());
    ASSERT_TRUE(future->IsFailed());
    ASSERT_EQ(future->GetError(), kReturnNotInit);
    ASSERT_TRUE(future->GetValue() == nullptr);
    delete future;
  }
  {
    Promise<uint32_t> promise;
    future = promise.GetFuture();
    ASSERT_FALSE(future->IsReady());
    ASSERT_FALSE(future->IsFailed());
    promise.SetValue(new uint32_t(42));
    ASSERT_TRUE(future->IsReady());
    ASSERT_FALSE(future->IsFailed());
    ASSERT_EQ(future->GetError(), kReturnOk);
    uint32_t *value = future->GetValue();
    ASSERT_TRUE(value != nullptr);
    ASSERT_EQ(*value, 42);
    delete value;
    ASSERT_TRUE(future->GetValue() == nullptr);
    delete future;
  }
}

void *ThreadFuncValue(void *args) {
  Promise<bool> *promise = static_cast<Promise<bool> *>(args);
  usleep(100 * 1000);
  promise->SetValue(new bool(true));
  delete promise;
  return nullptr;
}

void *ThreadFuncError(void *args) {
  Promise<bool> *promise = static_cast<Promise<bool> *>(args);
  usleep(100 * 1000);
  promise->SetError(kReturnResourceNotFound);
  delete promise;
  return nullptr;
}

TEST(FutureTest, SetPromise) {
  pthread_t tid;
  {  // 等待超时，正常删除
    Promise<bool> *promise = new Promise<bool>();
    Future<bool> *future = promise->GetFuture();
    pthread_create(&tid, nullptr, ThreadFuncValue, promise);
    ASSERT_TRUE(tid > 0);
    ASSERT_FALSE(future->Wait(10));
    ASSERT_FALSE(future->IsReady());
    delete future;
    pthread_join(tid, nullptr);
  }
  {  // 等待成功，结果失败
    Promise<bool> *promise = new Promise<bool>();
    Future<bool> *future = promise->GetFuture();
    pthread_create(&tid, nullptr, ThreadFuncValue, promise);
    ASSERT_TRUE(tid > 0);
    ASSERT_TRUE(future->Wait(200));
    ASSERT_TRUE(future->IsReady());
    ASSERT_FALSE(future->IsFailed());
    bool *result = future->GetValue();
    ASSERT_EQ(*result, true);
    delete result;
    delete future;
    pthread_join(tid, nullptr);
  }
  {  // 等待成功，结果失败
    Promise<bool> *promise = new Promise<bool>();
    Future<bool> *future = promise->GetFuture();
    pthread_create(&tid, nullptr, ThreadFuncError, promise);
    ASSERT_TRUE(tid > 0);
    ASSERT_TRUE(future->Wait(200));
    ASSERT_TRUE(future->IsReady());
    ASSERT_TRUE(future->IsFailed());
    delete future;
    pthread_join(tid, nullptr);
  }
}

void *ThreadFuncLoop(void *args) {
  Promise<bool> **promise_list = static_cast<Promise<bool> **>(args);

  for (int i = 0; i < 1000; i++) {
    Promise<bool> *promise = promise_list[i];
    promise->SetError(kReturnOk);
    delete promise;
  }

  return nullptr;
}

TEST(FutureTest, MultiThreadPromise) {
  Future<bool> *future_list[1000];
  Promise<bool> *promise_list[1000];

  for (int i = 0; i < 1000; ++i) {
    promise_list[i] = new Promise<bool>();
    future_list[i] = promise_list[i]->GetFuture();
  }

  pthread_t tid;
  ASSERT_EQ(pthread_create(&tid, nullptr, ThreadFuncLoop, &promise_list), 0);

  for (int i = 0; i < 1000; i++) {
    Future<bool> *future = future_list[i];
    future->Wait(2000);
    delete future;
  }
  pthread_join(tid, nullptr);
}

}  // namespace polaris
