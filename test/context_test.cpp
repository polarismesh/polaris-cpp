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

#include "context_internal.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace polaris {

class ContextTest : public ::testing::Test {
protected:
  virtual void SetUp() {
    context_ = NULL;
    config_  = NULL;
  }

  virtual void TearDown() {
    if (context_ != NULL) {
      delete context_;
      context_ = NULL;
    }
    if (config_ != NULL) {
      delete config_;
      config_ = NULL;
    }
  }

protected:
  Config *config_;
  Context *context_;
};

TEST_F(ContextTest, TestVerifyConfig) {
  std::string err_msg, content =
                           "consumer:\n"
                           "  loadBalancer:\n"
                           "    type: not_exist";
  config_ = Config::CreateFromString(content, err_msg);
  ASSERT_TRUE(config_ != NULL && err_msg.empty());
  context_ = Context::Create(config_);
  ASSERT_TRUE(context_ == NULL);  // 验证LB插件不正确，无法创建
}

}  // namespace polaris
