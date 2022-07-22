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

#include "utils/indestructible.h"

#include <gtest/gtest.h>

namespace polaris {

class NeverDelete {
 public:
  explicit NeverDelete(int value) : value_(value) {}

  ~NeverDelete() {
    if (value_ == 42) {
      EXPECT_EQ(Get1(), 111);
      EXPECT_EQ(Get2(), 222);
    } else {
      EXPECT_TRUE(false);  // 不会析构
    }
  }

  static int Get1() {
    static Indestructible<NeverDelete> data(111);
    return data.Get()->value_;
  }

  static int Get2() {
    static Indestructible<NeverDelete> data(222);
    return data.Get()->value_;
  }

  int Value() { return value_; }

 private:
  int value_;
};

TEST(IndestructibleTest, NeverDestructed) {
  Indestructible<NeverDelete> data(42);
  ASSERT_EQ(data.Get()->Value(), 42);
}

TEST(IndestructibleTest, StaticNeverDestructed) {
  // 如果Get1和Get2中的静态变量会析构则静态变量析构的顺序
  static NeverDelete data(42);
  ASSERT_EQ(data.Value(), 42);
  ASSERT_EQ(NeverDelete::Get1(), 111);
  ASSERT_EQ(NeverDelete::Get2(), 222);
}

struct __attribute__((__aligned__((128)))) AlignedData {
  int a;
  double b;
};

TEST(IndestructibleTest, AssertAlign) {  // 确保分配的内存能对齐
  std::size_t align_len = __alignof__(AlignedData);
  ASSERT_EQ(align_len, 128);
  Indestructible<AlignedData> data;
  std::size_t address = reinterpret_cast<std::size_t>(&data);
  ASSERT_EQ((address & (align_len - 1)), 0);
}

}  // namespace polaris
