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

#include "utils/scoped_ptr.h"

#include <gtest/gtest.h>

namespace polaris {

struct A {
  int a_;
};

TEST(ScopedPtrTest, Test) {
  ScopedPtr<A> data(new A());
  data->a_   = 1;
  (*data).a_ = 2;
  A* ptr     = data.Get();
  ASSERT_EQ(ptr->a_, 2);
  A* ptr2 = new A();
  data.Set(ptr2);
  delete ptr;
}

TEST(ScopedPtrTest, TestSize) {
  A* ptr = new A();
  ScopedPtr<A> data(ptr);
  ASSERT_EQ(sizeof(ptr), sizeof(data));
}

}  // namespace polaris
