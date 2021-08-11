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

#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "polaris/log.h"
#include "test_utils.h"

namespace polaris {

void (*current_time_impl_backup)(timespec &ts);
volatile uint64_t g_fake_time_now_ms = 0;
std::string g_test_persist_dir_;

};  // namespace polaris

// 自定义测试环境
class Environment : public ::testing::Environment {
public:
  virtual ~Environment() {}

  virtual void SetUp() {
    polaris::TestUtils::CreateTempDir(log_dir_);
    polaris::GetLogger()->SetLogDir(log_dir_);
    polaris::GetStatLogger()->SetLogDir(log_dir_);
    // printf("set log dir to %s\n", log_dir_.c_str());
    polaris::TestUtils::CreateTempDir(polaris::g_test_persist_dir_);
  }

  virtual void TearDown() {
    if (!log_dir_.empty()) {
      polaris::TestUtils::RemoveDir(log_dir_);
    }
    if (!polaris::g_test_persist_dir_.empty()) {
      polaris::TestUtils::RemoveDir(polaris::g_test_persist_dir_);
    }
  }

private:
  std::string log_dir_;
};

GTEST_API_ int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new Environment());
  return RUN_ALL_TESTS();
}
