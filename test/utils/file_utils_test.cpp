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

#include "utils/file_utils.h"

#include <gtest/gtest.h>
#include <stdlib.h>

#include <string>

#include "test_utils.h"

namespace polaris {

TEST(FileUtilsTest, TestExpandPath) {
  int ret = setenv("POLARIS_TEST", "TEST", 1);
  EXPECT_EQ(ret, 0);
  std::string expand_path = FileUtils::ExpandPath("$POLARIS_TEST");
  EXPECT_EQ(expand_path, "TEST");
  expand_path = FileUtils::ExpandPath("$POLARIS_TEST/test");
  EXPECT_EQ(expand_path, "TEST/test");
  expand_path = FileUtils::ExpandPath("test/$POLARIS_TEST");
  EXPECT_EQ(expand_path, "test/TEST");

  ret = setenv("POLARIS_TEST2", "test2", 1);
  EXPECT_EQ(ret, 0);
  expand_path = FileUtils::ExpandPath("$POLARIS_TEST/$POLARIS_TEST2");
  EXPECT_EQ(expand_path, "TEST/test2");
  expand_path = FileUtils::ExpandPath("$POLARIS_TEST/test/$POLARIS_TEST2");
  EXPECT_EQ(expand_path, "TEST/test/test2");

  expand_path = FileUtils::ExpandPath("test/$POLARIS_TEST_NOT_EXISTS");
  EXPECT_EQ(expand_path, "test/");
}

TEST(FileUtilsTest, TestHomePathExpand) {
  char* home_path = getenv("HOME");
  EXPECT_TRUE(home_path != NULL);
  std::string home_str(home_path);
  std::string expand_path = FileUtils::ExpandPath("$HOME/test");
  EXPECT_EQ(expand_path, home_str + "/test");

  unsetenv("HOME");
  home_path = getenv("HOME");
  ASSERT_TRUE(home_path == NULL);
  expand_path = FileUtils::ExpandPath("$HOME/test");
  EXPECT_EQ(expand_path, home_str + "/test");
}

TEST(FileUtilsTest, TestCreatePath) {
  uint64_t current_time = Time::GetCurrentTimeMs();
  std::string path =
      "/tmp/polaris_test/" + StringUtils::TypeToStr<uint64_t>(current_time) + "/create_path/test";
  bool result = FileUtils::CreatePath(path);
  ASSERT_EQ(result, true);
  ASSERT_TRUE(FileUtils::FileExists(path));

  path =
      "/tmp/polaris_test//" + StringUtils::TypeToStr<uint64_t>(current_time) + "//create_path/test";
  result = FileUtils::CreatePath(path);
  ASSERT_EQ(result, true);
  ASSERT_TRUE(FileUtils::FileExists(path));

  // 重复创建
  result = FileUtils::CreatePath(path);
  ASSERT_EQ(result, true);

  TestUtils::RemoveDir("/tmp/polaris_test/");
}

}  // namespace polaris
