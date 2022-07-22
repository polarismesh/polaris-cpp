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

#ifndef POLARIS_CPP_POLARIS_UTILS_REF_COUNT_H_
#define POLARIS_CPP_POLARIS_UTILS_REF_COUNT_H_

#include "utils/utils.h"

namespace polaris {

class RefCount {
 public:
  RefCount() { ref_count_ = 1; }

  void IncrementRef() { ref_count_++; }

  void DecrementRef() {
    if (--ref_count_ == 0) {
      delete this;
    }
  }

 protected:
  virtual ~RefCount() {}

  int ref_count_;
};

class AtomicRefCount {
 public:
  AtomicRefCount() : ref_count_(1) {}

  void IncrementRef() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

  void DecrementRef() {
    int pre_count = ref_count_.fetch_sub(1, std::memory_order_acq_rel);
    if (pre_count == 1) {
      delete this;
    }
  }

 protected:
  virtual ~AtomicRefCount() {}

  std::atomic<int> ref_count_;
};

/// @brief 引用计数基类，用于只需要使用RefPtr的类型
template <class T>
class RefBase {
 public:
  inline RefBase() : ref_count_(0) {}

  inline void IncRef() { ref_count_++; }

  inline void DecRef() {
    int32_t ref_before = ref_count_--;
    if (ref_before == 1) {
      delete static_cast<const T *>(this);
    }
  }

  //! DEBUGGING ONLY
  int32_t GetRefCount() const { return ref_count_; }

 protected:
  inline ~RefBase() {}

 private:
  std::atomic<int32_t> ref_count_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_UTILS_REF_COUNT_H_
