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

#ifndef POLARIS_UTILS_SHARED_PTR_H_
#define POLARIS_UTILS_SHARED_PTR_H_

#include <stddef.h>

#include <algorithm>  // for std::swap

#include "sync/atomic.h"

namespace polaris {

template <typename T>
class SharedPtr {
public:
  SharedPtr() : ptr_(NULL), refcount_(NULL) {}

  explicit SharedPtr(T* ptr)
      : ptr_(ptr), refcount_(ptr != NULL ? new sync::Atomic<int>(1) : NULL) {}

  SharedPtr(const SharedPtr<T>& ptr) : ptr_(NULL), refcount_(NULL) { Initialize(ptr); }

  SharedPtr<T>& operator=(const SharedPtr<T>& ptr) {
    if (ptr_ != ptr.ptr_) {
      SharedPtr<T> me(ptr);  // 用于释放之前的内存
      Swap(me);
    }
    return *this;
  }

  ~SharedPtr() {
    if (ptr_ != NULL) {
      if (--(*refcount_) == 0) {
        delete ptr_;
        delete refcount_;
      }
    }
  }

  void Swap(SharedPtr<T>& r) {
    std::swap(ptr_, r.ptr_);
    std::swap(refcount_, r.refcount_);
  }

  void Reset(T* p) {
    if (p != ptr_) {
      SharedPtr<T> tmp(p);
      tmp.Swap(*this);
    }
  }

  void Reset() { Reset(static_cast<T*>(NULL)); }

  bool NotNull() const { return ptr_ != NULL; }

  bool IsNull() const { return ptr_ == NULL; }

  T* Get() const { return ptr_; }

  T& operator*() const { return *ptr_; }

  T* operator->() const { return ptr_; }

private:
  void Initialize(const SharedPtr<T>& r) {
    if (r.refcount_ != NULL) {
      (*r.refcount_)++;
      ptr_      = r.ptr_;
      refcount_ = r.refcount_;
    }
  }

  T* ptr_;
  sync::Atomic<int>* refcount_;
};

}  // namespace polaris

#endif  // POLARIS_UTILS_SHARED_PTR_H_
