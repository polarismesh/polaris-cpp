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

#include "integration/common/environment.h"

#include <errno.h>
#include <error.h>
#include <stdio.h>

#include <string>

#include "polaris/log.h"
#include "test_utils.h"

namespace polaris {

static const char POLARIS_SERVER_ENV[] = "POLARIS_SERVER";
static const char POLARIS_USER_ENV[]   = "POLARIS_USER";

std::string Environment::persist_dir_;
std::string Environment::polaris_server_;

void Environment::SetUp() {
  char* env = getenv(POLARIS_SERVER_ENV);
  ASSERT_TRUE(env != NULL) << "get env POLARIS_SERVER errno:" << errno;
  polaris_server_.assign(env);
  polaris::TestUtils::CreateTempDir(log_dir_);
  polaris::GetLogger()->SetLogDir(log_dir_);
  polaris::GetStatLogger()->SetLogDir(log_dir_);
  polaris::TestUtils::CreateTempDir(persist_dir_);
  polaris::GetLogger()->SetLogLevel(kTraceLogLevel);
  // printf("set log dir to %s, persist log dir:%s\n", log_dir_.c_str(), persist_dir_.c_str());
}

void Environment::TearDown() {
  if (!log_dir_.empty()) {
    polaris::TestUtils::RemoveDir(log_dir_);
  }
  if (!persist_dir_.empty()) {
    polaris::TestUtils::RemoveDir(persist_dir_);
  }
}

void Environment::GetConsoleServer(std::string& host, int& port) {
  host = polaris_server_;
  port = 8080;
}

std::string Environment::GetDiscoverServer() { return polaris_server_ + ":8081"; }

std::string Environment::GetPolarisUser() {
  char* env = getenv(POLARIS_USER_ENV);
  EXPECT_TRUE(env != NULL) << "get env POLARIS_USER errno:" << errno;
  return env;
}

}  // namespace polaris

GTEST_API_ int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new polaris::Environment());
  return RUN_ALL_TESTS();
}
