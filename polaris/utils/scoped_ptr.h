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

#ifndef POLARIS_CPP_POLARIS_UTILS_SCOPED_PTR_H_
#define POLARIS_CPP_POLARIS_UTILS_SCOPED_PTR_H_

#include <stddef.h>

#include "polaris/noncopyable.h"

namespace polaris {

template <class T>
class ScopedPtr : public Noncopyable {
public:
  explicit ScopedPtr(T* ptr = NULL) : ptr_(ptr) {}

  // c++ 允许 delete NULL
  ~ScopedPtr() { delete ptr_; }

  T* Release() {
    T* retVal = ptr_;
    ptr_      = NULL;
    return retVal;
  }

  void Set(T* p) { ptr_ = p; }

  // 删除旧指针，重置成新指针
  void Reset(T* p = NULL) {
    if (p != ptr_) {  // 防止重置成自己的时候被删除
      delete ptr_;
      ptr_ = p;
    }
  }

  bool NotNull() const { return ptr_ != NULL; }

  bool IsNull() const { return ptr_ == NULL; }

  T* Get() const { return ptr_; }

  T& operator*() const { return *ptr_; }

  T* operator->() const { return ptr_; }

private:
  T* ptr_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_UTILS_SCOPED_PTR_H_
