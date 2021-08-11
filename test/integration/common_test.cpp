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

#include "integration/common/http_client.h"
#include "integration/common/integration_base.h"

namespace polaris {

class CommonTest : public IntegrationBase {
protected:
  virtual void SetUp() {}

  virtual void TearDown() {}
};

TEST_F(CommonTest, TestHttpClient) {
  std::string response;
  int ret_code = HttpClient::DoRequest(HTTP_GET, "/", "", 1000, response);
  ASSERT_EQ(ret_code, 200);
  ASSERT_EQ(response, "Polaris Server");
}

}  // namespace polaris
