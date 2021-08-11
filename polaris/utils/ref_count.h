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
  AtomicRefCount() { ref_count_ = 1; }

  void IncrementRef() { ATOMIC_INC(&ref_count_); }

  void DecrementRef() {
    int pre_count = ATOMIC_DEC(&ref_count_);
    if (pre_count == 1) {
      delete this;
    }
  }

protected:
  virtual ~AtomicRefCount() {}

  volatile int ref_count_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_UTILS_REF_COUNT_H_
