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

#ifndef POLARIS_CPP_POLARIS_UTILS_INDESTRUCTIBLE_H_
#define POLARIS_CPP_POLARIS_UTILS_INDESTRUCTIBLE_H_

#include <stddef.h>
#include <cstdlib>
#if __cplusplus >= 201103L
#include <type_traits>
#endif

#include "polaris/noncopyable.h"

namespace polaris {

template <std::size_t Size, std::size_t Align>
struct IndestructibleStorage {
  struct Type {
    __attribute__((__aligned__((Align)))) unsigned char storage_[Size];
  };
};

// 用于保存永不释放的全局对象
template <class T>
class Indestructible : public Noncopyable {
public:
  Indestructible() { new (&this->storage_) T(); }

  // 只支持一个参数的析构构造函数，多个参数则使用结构体传入
  template <typename P>
  explicit Indestructible(P p) {
    new (&this->storage_) T(p);
  }

  T* Get() { return reinterpret_cast<T*>(&storage_); }

  const T* Get() const { return reinterpret_cast<const T*>(&storage_); }

private:
#if __cplusplus >= 201103L
  typename std::aligned_storage<sizeof(T), alignof(T)>::type storage_;
#else
  typename IndestructibleStorage<sizeof(T), __alignof__(T)>::Type storage_;
#endif
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_UTILS_INDESTRUCTIBLE_H_
