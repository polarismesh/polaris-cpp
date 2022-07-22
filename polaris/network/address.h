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

#ifndef POLARIS_CPP_POLARIS_NETWORK_ADDRESS_H_
#define POLARIS_CPP_POLARIS_NETWORK_ADDRESS_H_

#include <arpa/inet.h>

#include <string>

namespace polaris {

class NetworkAddress {
 public:
  NetworkAddress();

  ~NetworkAddress() = default;

  NetworkAddress(const NetworkAddress &addr) = default;

  NetworkAddress &operator=(const NetworkAddress &addr) = default;

  explicit NetworkAddress(const struct sockaddr *addr);

  explicit NetworkAddress(uint16_t port, bool loopback, bool ipv6 = false);

  explicit NetworkAddress(const std::string &ip, uint16_t port);

  explicit NetworkAddress(const std::string &ip_port);

  std::string ToString() const;

  std::string Ip() const;

  uint16_t Port() const;

  sa_family_t Family() const { return addr_in_.sin_family; }

  bool IsValid() const { return addr_in_.sin_family != PF_UNSPEC; }

  bool IsIpv4() const { return addr_in_.sin_family == AF_INET; }

  bool IsIpv6() const { return addr_in_.sin_family == AF_INET6; }

  struct sockaddr *Sockaddr();

  const struct sockaddr *Sockaddr() const;

  socklen_t Socklen() const;

  explicit operator bool() const { return addr_in_.sin_family != PF_UNSPEC; }

  friend bool operator==(const NetworkAddress &l, const NetworkAddress &r);

  friend bool operator!=(const NetworkAddress &l, const NetworkAddress &r);

  friend bool operator<(const NetworkAddress &l, const NetworkAddress &r);

 private:
  union {
    struct sockaddr_in addr_in_;
    struct sockaddr_in6 addr_in6_;
  };
};

}  // namespace polaris

#endif  // POLARIS_CPP_POLARIS_NETWORK_ADDRESS_H_