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

#include "utils/string_utils.h"

#include <gtest/gtest.h>

namespace polaris {

TEST(StringUtilsTest, Translate) {
  std::string str;
  int result = 0;
  ASSERT_TRUE(StringUtils::SafeStrToType("42", result));
  ASSERT_EQ(result, 42);

  result = 16;
  ASSERT_FALSE(StringUtils::SafeStrToType(":42", result));  //失败时不改变原先的值
  ASSERT_EQ(result, 16);
}

TEST(StringUtilsTest, IgnoreCaseCmp) {
  ASSERT_TRUE(StringUtils::IgnoreCaseCmp("ABC", "abc"));
  ASSERT_TRUE(StringUtils::IgnoreCaseCmp("AbC", "abc"));
  ASSERT_TRUE(StringUtils::IgnoreCaseCmp("ABc", "abc"));

  ASSERT_FALSE(StringUtils::IgnoreCaseCmp("ABCd", "abc"));
  ASSERT_FALSE(StringUtils::IgnoreCaseCmp("AbC", "abcD"));
  ASSERT_FALSE(StringUtils::IgnoreCaseCmp("ABc", "a"));
  ASSERT_FALSE(StringUtils::IgnoreCaseCmp("", "abc"));
  ASSERT_FALSE(StringUtils::IgnoreCaseCmp("abc", ""));

  ASSERT_TRUE(StringUtils::IgnoreCaseCmp(":42~", ":42~"));
}

TEST(StringUtilsTest, TestStringTrim) {
  ASSERT_EQ(StringUtils::StringTrim("   "), "");
  ASSERT_EQ(StringUtils::StringTrim("  C "), "C");
  ASSERT_EQ(StringUtils::StringTrim("L   "), "L");
  ASSERT_EQ(StringUtils::StringTrim("   R"), "R");
}

TEST(StringUtilsTest, TestStringHasSuffix) {
  ASSERT_TRUE(StringUtils::StringHasSuffix("", ""));
  ASSERT_TRUE(StringUtils::StringHasSuffix("  ", ""));
  ASSERT_TRUE(StringUtils::StringHasSuffix("ABCD", ""));
  ASSERT_TRUE(StringUtils::StringHasSuffix("ABCD", "D"));
  ASSERT_TRUE(StringUtils::StringHasSuffix("ABCD", "CD"));
  ASSERT_TRUE(StringUtils::StringHasSuffix("ABCD", "ABCD"));

  ASSERT_FALSE(StringUtils::StringHasSuffix("ABCD", "ZABCD"));
  ASSERT_FALSE(StringUtils::StringHasSuffix("ABCD", "A"));
  ASSERT_FALSE(StringUtils::StringHasSuffix("", "A"));
  ASSERT_FALSE(StringUtils::StringHasSuffix("", "AB"));
}

TEST(StringUtilsTest, TestJoinString) {
  std::vector<std::string> list;
  ASSERT_EQ(StringUtils::JoinString(list), "");

  list.push_back("0");
  ASSERT_EQ(StringUtils::JoinString(list), "0");

  list.push_back("1");
  ASSERT_EQ(StringUtils::JoinString(list), "0, 1");

  list.push_back("2");
  ASSERT_EQ(StringUtils::JoinString(list), "0, 1, 2");
}

}  // namespace polaris
