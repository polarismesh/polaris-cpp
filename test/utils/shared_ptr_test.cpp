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

#include "utils/shared_ptr.h"

#include <gtest/gtest.h>

namespace polaris {

struct A {
  int a_;
};

TEST(SharedPtrTest, Constructor) {
  SharedPtr<A> data1(new A());
  data1->a_ = 1;

  SharedPtr<A> data2(data1);
  data2->a_ = 2;
  ASSERT_EQ((*data1).a_, data2->a_);
}

TEST(SharedPtrTest, Assignment) {
  SharedPtr<A> data1(new A());
  data1->a_ = 1;

  SharedPtr<A> data2;
  data2.Reset(new A());
  data2->a_ = 2;

  data1 = data2;
  ASSERT_EQ(data1->a_, data1->a_);
  ASSERT_EQ(data1.Get(), data2.Get());
}

TEST(SharedPtrTest, TestSwap) {
  SharedPtr<A> data1(new A());
  data1->a_ = 1;

  SharedPtr<A> data2;
  data2.Reset(new A());
  data2->a_ = 2;
  data2.Swap(data1);
  ASSERT_EQ(data2->a_, 1);
  ASSERT_EQ(data1->a_, 2);
}

}  // namespace polaris
