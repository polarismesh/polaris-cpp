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

#include "utils/ip_utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stddef.h>
#include <sys/socket.h>

namespace polaris {
bool IpUtils::IntIpToStr(uint32_t int_ip, std::string& str_ip) {
  char buf[16] = {0};
  struct in_addr sin_addr;
  sin_addr.s_addr = int_ip;
  if (inet_ntop(AF_INET, static_cast<void*>(&sin_addr.s_addr), buf, sizeof(buf)) != NULL) {
    str_ip = buf;
    return true;
  } else {
    return false;
  }
}

bool IpUtils::StrIpToInt(const std::string& str_ip, uint32_t& int_ip) {
  struct in_addr sin_addr;
  sin_addr.s_addr = 0;
  if (inet_pton(AF_INET, str_ip.c_str(), &sin_addr.s_addr) > 0) {  // return 1 on success
    int_ip = sin_addr.s_addr;
    return true;
  } else {
    return false;
  }
}

}  // namespace polaris
