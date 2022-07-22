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
#include <pthread.h>
#include <unistd.h>

#include "re2/re2.h"

namespace polaris {
namespace grpc {

class GrpcDomainTest : public ::testing::Test {
 protected:
  virtual void SetUp() {}

  virtual void TearDown() {}

 protected:
};

TEST_F(GrpcDomainTest, DomainJudge) {
  std::string domain_reg = "((2(5[0-5]|[0-4]\\d))|[0-1]?\\d{1,2})(\\.((2(5[0-5]|[0-4]\\d))|[0-1]?\\d{1,2})){3}";
  re2::RE2 regex_(domain_reg.c_str());
  ASSERT_TRUE((regex_).ok());
  std::string str_1 = "baidu.com";
  ASSERT_FALSE(re2::RE2::PartialMatch(str_1.c_str(), regex_));

  std::string str_2 = "polaris.default.svc.local";
  ASSERT_FALSE(re2::RE2::PartialMatch(str_2.c_str(), regex_));

  std::string str_3 = "127.0.0.1";
  ASSERT_TRUE(re2::RE2::PartialMatch(str_3.c_str(), regex_));
}

}  // namespace grpc
}  // namespace polaris