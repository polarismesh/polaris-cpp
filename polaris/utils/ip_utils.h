//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
//  language governing permissions and limitations under the License.
//

#ifndef POLARIS_CPP_POLARIS_UTILS_IP_UTILS_H_
#define POLARIS_CPP_POLARIS_UTILS_IP_UTILS_H_

#include <stdint.h>

#include <string>

namespace polaris {

class IpUtils {
 public:
  // 将int格式的IP转换成string类型
  static bool IntIpToStr(uint32_t int_ip, std::string& str_ip);

  // 将string格式的IP转换成INT类型
  static bool StrIpToInt(const std::string& str_ip, uint32_t& int_ip);
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_UTILS_IP_UTILS_H_
