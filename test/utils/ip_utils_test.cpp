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

#include "utils/ip_utils.h"

#include <gtest/gtest.h>

#include <stdlib.h>
#include <time.h>

#include <string>

namespace polaris {

TEST(IPUtilsTest, Translate) {
  std::string str_ip;
  uint32_t input_ip;
  uint32_t output_ip;
  srand(time(nullptr));
  for (uint32_t i = 0; i < 1000000; ++i) {
    input_ip = rand() & 0xffffffff;
    ASSERT_TRUE(IpUtils::IntIpToStr(input_ip, str_ip));
    ASSERT_TRUE(IpUtils::StrIpToInt(str_ip, output_ip));
    ASSERT_EQ(output_ip, input_ip);
  }
}

}  // namespace polaris
