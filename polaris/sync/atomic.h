//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
//  language governing permissions and limitations under the License.
//

#ifndef POLARIS_CPP_POLARIS_SYNC_ATOMIC_H_
#define POLARIS_CPP_POLARIS_SYNC_ATOMIC_H_

#include "polaris/noncopyable.h"
#include "utils/static_assert.h"

// 如果支持c++11则使用c++11的原子变量
// 如果支持GCC：
//     GCC版本大于等于4.7则使用__atomic_xxx指令
//     GCC版本小于4.7则使用__sync_xxx指令
// 否则编译报错
#if __cplusplus >= 201103L
#define ATOMIC_USE_CPP11_ATOMIC
#include <atomic>
#elif defined(__GNUC__)
#if (__GNUC__ * 100 + __GNUC_MINOR__ >= 407)
#define ATOMIC_USE_GCC_ATOMIC
#else
#define ATOMIC_USE_GCC_SYNC
#endif
#else
#error Unsupported compiler
#endif

namespace polaris {

namespace sync {

template <typename T>
class Atomic : Noncopyable {
public:
  STATIC_ASSERT(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                "Only types of size 1, 2, 4 or 8 are supported");

  Atomic() : value_(static_cast<T>(0)) {}

  explicit Atomic(const T value) : value_(value) {}

  T operator++(int) {
#if defined(ATOMIC_USE_GCC_ATOMIC)
    return __atomic_fetch_add(&value_, 1, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_GCC_SYNC)
    return __sync_fetch_and_add(&value_, 1);
#else
    return value_++;
#endif
  }

  T operator++() {
#if defined(ATOMIC_USE_GCC_ATOMIC)
    return __atomic_add_fetch(&value_, 1, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_GCC_SYNC)
    return __sync_add_and_fetch(&value_, 1);
#else
    return ++value_;
#endif
  }

  T operator--(int) {
#if defined(ATOMIC_USE_GCC_ATOMIC)
    return __atomic_fetch_sub(&value_, 1, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_GCC_SYNC)
    return __sync_fetch_and_sub(&value_, 1);
#else
    return value_--;
#endif
  }

  T operator--() {
#if defined(ATOMIC_USE_GCC_ATOMIC)
    return __atomic_sub_fetch(&value_, 1, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_GCC_SYNC)
    return __sync_sub_and_fetch(&value_, 1);
#else
    return --value_;
#endif
  }

  T operator+=(T v) {
#if defined(ATOMIC_USE_GCC_ATOMIC)
    return __atomic_add_fetch(&value_, v, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_GCC_SYNC)
    return __sync_add_and_fetch(&value_, v);
#else
    return value_ += v;
#endif
  }

  T operator-=(T v) {
#if defined(ATOMIC_USE_GCC_ATOMIC)
    return __atomic_sub_fetch(&value_, v, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_GCC_SYNC)
    return __sync_sub_and_fetch(&value_, v);
#else
    return value_ -= v;
#endif
  }

  T operator&=(T v) {
#if defined(ATOMIC_USE_GCC_ATOMIC)
    return __atomic_and_fetch(&value_, v, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_GCC_SYNC)
    return __sync_and_and_fetch(&value_, v);
#else
    return value_ &= v;
#endif
  }

  T operator|=(T v) {
#if defined(ATOMIC_USE_GCC_ATOMIC)
    return __atomic_or_fetch(&value_, v, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_GCC_SYNC)
    return __sync_or_and_fetch(&value_, v);
#else
    return value_ |= v;
#endif
  }

  T operator^=(T v) {
#if defined(ATOMIC_USE_GCC_ATOMIC)
    return __atomic_xor_fetch(&value_, v, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_GCC_SYNC)
    return __sync_xor_and_fetch(&value_, v);
#else
    return value_ ^= v;
#endif
  }

  bool Cas(const T expected_val, const T new_val) {
#if defined(ATOMIC_USE_GCC_ATOMIC)
    T e = expected_val;
    return __atomic_compare_exchange_n(&value_, &e, new_val, true, __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_GCC_SYNC)
    return __sync_bool_compare_and_swap(&value_, expected_val, new_val);
#else
    T e = expected_val;
    return value_.compare_exchange_weak(e, new_val);
#endif
  }

  void Store(const T new_val) {
#if defined(ATOMIC_USE_GCC_ATOMIC)
    __atomic_store_n(&value_, new_val, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_GCC_SYNC)
    __sync_synchronize();
    value_ = new_val;
    __sync_synchronize();
#else
    value_.store(new_val);
#endif
  }

  T Load() const {
#if defined(ATOMIC_USE_GCC_ATOMIC)
    return __atomic_load_n(&value_, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_GCC_SYNC)
    T value = value_;
    __sync_synchronize();
    return value;
#else
    return value_;
#endif
  }

  T Exchange(const T new_val) {
#if defined(ATOMIC_USE_GCC_ATOMIC)
    return __atomic_exchange_n(&value_, new_val, __ATOMIC_SEQ_CST);
#elif defined(ATOMIC_USE_GCC_SYNC)
    __sync_synchronize();
    return __sync_lock_test_and_set(&value_, new_val);
#else
    return value_.exchange(new_val);
#endif
  }

  T operator=(const T new_value) {
    Store(new_value);
    return new_value;
  }

  operator T() const { return Load(); }

private:
#ifdef ATOMIC_USE_CPP11_ATOMIC
  std::atomic<T> value_;
#else
  volatile T value_;
#endif
};

}  // namespace sync

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_SYNC_ATOMIC_H_
