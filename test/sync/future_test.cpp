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
  Future<uint32_t> *future = NULL;
  {
    Promise<uint32_t> promise;
    future = promise.GetFuture();
    ASSERT_FALSE(future->IsReady());
    ASSERT_FALSE(future->IsFailed());
    promise.SetError(kReturnNotInit);
    ASSERT_TRUE(future->IsReady());
    ASSERT_TRUE(future->IsFailed());
    ASSERT_EQ(future->GetError(), kReturnNotInit);
    ASSERT_TRUE(future->GetValue() == NULL);
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
    ASSERT_TRUE(value != NULL);
    ASSERT_EQ(*value, 42);
    delete value;
    ASSERT_TRUE(future->GetValue() == NULL);
    delete future;
  }
}

void *ThreadFuncValue(void *args) {
  Promise<bool> *promise = static_cast<Promise<bool> *>(args);
  usleep(100 * 1000);
  promise->SetValue(new bool(true));
  delete promise;
  return NULL;
}

void *ThreadFuncError(void *args) {
  Promise<bool> *promise = static_cast<Promise<bool> *>(args);
  usleep(100 * 1000);
  promise->SetError(kReturnResourceNotFound);
  delete promise;
  return NULL;
}

TEST(FutureTest, SetPromise) {
  pthread_t tid;
  {  // 等待超时，正常删除
    Promise<bool> *promise = new Promise<bool>();
    Future<bool> *future   = promise->GetFuture();
    pthread_create(&tid, NULL, ThreadFuncValue, promise);
    ASSERT_TRUE(tid > 0);
    ASSERT_FALSE(future->Wait(10));
    ASSERT_FALSE(future->IsReady());
    delete future;
    pthread_join(tid, NULL);
  }
  {  // 等待成功，结果失败
    Promise<bool> *promise = new Promise<bool>();
    Future<bool> *future   = promise->GetFuture();
    pthread_create(&tid, NULL, ThreadFuncValue, promise);
    ASSERT_TRUE(tid > 0);
    ASSERT_TRUE(future->Wait(200));
    ASSERT_TRUE(future->IsReady());
    ASSERT_FALSE(future->IsFailed());
    bool *result = future->GetValue();
    ASSERT_EQ(*result, true);
    delete result;
    delete future;
    pthread_join(tid, NULL);
  }
  {  // 等待成功，结果失败
    Promise<bool> *promise = new Promise<bool>();
    Future<bool> *future   = promise->GetFuture();
    pthread_create(&tid, NULL, ThreadFuncError, promise);
    ASSERT_TRUE(tid > 0);
    ASSERT_TRUE(future->Wait(200));
    ASSERT_TRUE(future->IsReady());
    ASSERT_TRUE(future->IsFailed());
    delete future;
    pthread_join(tid, NULL);
  }
}

}  // namespace polaris
