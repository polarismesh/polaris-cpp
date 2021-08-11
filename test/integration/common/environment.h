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

#ifndef POLARIS_CPP_TEST_INTEGRATION_COMMON_ENVIRONMENT_H_
#define POLARIS_CPP_TEST_INTEGRATION_COMMON_ENVIRONMENT_H_

#include <gtest/gtest.h>

#include <string>

namespace polaris {

// 自定义测试环境
class Environment : public ::testing::Environment {
public:
  virtual ~Environment() {}

  virtual void SetUp();

  virtual void TearDown();

  /// @brief 获取测试用的持久化目录
  static const std::string& GetPersistDir() { return persist_dir_; }

  /// @brief 获取测试用的控制台服务器
  static void GetConsoleServer(std::string& host, int& port);

  /// @brief 获取测试用的发现服务器
  static std::string GetDiscoverServer();

  /// @brief 获取测试接口使用的用户名
  static std::string GetPolarisUser();

private:
  std::string log_dir_;
  static std::string persist_dir_;
  static std::string polaris_server_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_INTEGRATION_COMMON_ENVIRONMENT_H_
