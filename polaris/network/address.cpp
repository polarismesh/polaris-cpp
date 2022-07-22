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

#include "network/address.h"

#include <string.h>

#include <tuple>
#include <utility>

namespace polaris {

NetworkAddress::NetworkAddress() { addr_in_.sin_family = PF_UNSPEC; }

NetworkAddress::NetworkAddress(const struct sockaddr *addr) {
  switch (addr->sa_family) {
    case AF_INET:
      addr_in_ = *reinterpret_cast<const sockaddr_in *>(addr);
      break;
    case AF_INET6:
      addr_in6_ = *reinterpret_cast<const sockaddr_in6 *>(addr);
      break;
    default:
      addr_in_.sin_family = PF_UNSPEC;
      break;
  }
}

NetworkAddress::NetworkAddress(uint16_t port, bool loopback, bool ipv6) {
  if (ipv6) {
    bzero(static_cast<void *>(&addr_in6_), sizeof(struct sockaddr_in6));
    addr_in6_.sin6_family = AF_INET6;
    addr_in6_.sin6_port = htons(port);
    addr_in6_.sin6_addr = loopback ? in6addr_loopback : in6addr_any;
    addr_in6_.sin6_flowinfo = 0;
    addr_in6_.sin6_scope_id = 0;
  } else {
    bzero(static_cast<void *>(&addr_in_), sizeof(struct sockaddr_in));
    addr_in_.sin_family = AF_INET;
    addr_in_.sin_port = htons(port);
    addr_in_.sin_addr.s_addr = loopback ? htonl(INADDR_LOOPBACK) : htonl(INADDR_ANY);
  }
}

/*
 * ipv4 format: xxx.xxx.xxx.xxx
 * ipv6 format: xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx
 */
NetworkAddress::NetworkAddress(const std::string &ip, uint16_t port) {
  // 先尝试IPv4
  bzero(static_cast<void *>(&addr_in_), sizeof(struct sockaddr_in));
  if (inet_pton(AF_INET, ip.data(), &(addr_in_.sin_addr)) > 0) {
    addr_in_.sin_family = AF_INET;
    addr_in_.sin_port = htons(port);
    return;
  }
  // 再尝试IPv6
  bzero(static_cast<void *>(&addr_in6_), sizeof(struct sockaddr_in6));
  if (inet_pton(AF_INET6, ip.data(), &(addr_in6_.sin6_addr)) > 0) {
    addr_in6_.sin6_family = AF_INET6;
    addr_in6_.sin6_port = htons(port);
    addr_in6_.sin6_flowinfo = 0;
    addr_in6_.sin6_scope_id = 0;
    return;
  }
  addr_in_.sin_family = AF_UNSPEC;  // 解析失败
}

/*
 * ipv4 format: xxx.xxx.xxx.xxx:yyy
 * ipv6 format: [xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx]:yyy
 */
NetworkAddress::NetworkAddress(const std::string &ip_port) {
  if (!ip_port.empty() && ip_port[0] == '[') {  // 先尝试IPv6
    auto pos = ip_port.find("]:");
    if (pos != std::string ::npos) {
      const std::string &ip = ip_port.substr(1, pos - 1);
      const std::string &port = ip_port.substr(pos + 2);
      bzero(static_cast<void *>(&addr_in6_), sizeof(struct sockaddr_in6));
      if (inet_pton(AF_INET6, ip.data(), &(addr_in6_.sin6_addr)) > 0) {
        addr_in6_.sin6_family = AF_INET6;
        addr_in6_.sin6_port = htons(static_cast<uint16_t>(atoi(port.data())));
        addr_in6_.sin6_flowinfo = 0;
        addr_in6_.sin6_scope_id = 0;
        return;
      }
    }
  } else {  // 再尝试IPv4
    auto pos = ip_port.find(":");
    if (pos != std::string::npos) {
      const std::string &ip = ip_port.substr(0, pos);
      const std::string &port = ip_port.substr(pos + 1);
      bzero(static_cast<void *>(&addr_in_), sizeof(struct sockaddr_in));
      if (inet_pton(AF_INET, ip.data(), &(addr_in_.sin_addr)) > 0) {
        addr_in_.sin_family = AF_INET;
        addr_in_.sin_port = htons(static_cast<uint16_t>(atoi(port.data())));
        return;
      }
    }
  }
  addr_in6_.sin6_family = AF_UNSPEC;
}

std::string NetworkAddress::ToString() const {
  switch (Family()) {
    case AF_INET:
      return Ip() + ":" + std::to_string(Port());
    case AF_INET6:
      return "[" + Ip() + "]:" + std::to_string(Port());
    default:
      return "";
  }
}

std::string NetworkAddress::Ip() const {
  std::string str;
  switch (Family()) {
    case AF_INET: {
      char buf[INET_ADDRSTRLEN];
      return inet_ntop(AF_INET, reinterpret_cast<const void *>(&addr_in_.sin_addr), buf, INET_ADDRSTRLEN);
    }
    case AF_INET6: {
      char buf[INET6_ADDRSTRLEN];
      return inet_ntop(AF_INET6, reinterpret_cast<const void *>(&addr_in6_.sin6_addr), buf, INET6_ADDRSTRLEN);
    }
    default:
      return "";
  }
}

uint16_t NetworkAddress::Port() const {
  switch (Family()) {
    case AF_INET:
      return ntohs(addr_in_.sin_port);
    case AF_INET6:
      return ntohs(addr_in6_.sin6_port);
    default:
      return 0;
  }
}

struct sockaddr *NetworkAddress::Sockaddr() {
  switch (Family()) {
    case AF_INET:
      return reinterpret_cast<struct sockaddr *>(&addr_in_);
    case AF_INET6:
      return reinterpret_cast<struct sockaddr *>(&addr_in6_);
    default:
      return nullptr;
  }
}

const struct sockaddr *NetworkAddress::Sockaddr() const {
  switch (Family()) {
    case AF_INET:
      return reinterpret_cast<const struct sockaddr *>(&addr_in_);
    case AF_INET6:
      return reinterpret_cast<const struct sockaddr *>(&addr_in6_);
    default:
      return nullptr;
  }
}

socklen_t NetworkAddress::Socklen() const {
  switch (Family()) {
    case AF_INET: {
      constexpr socklen_t length = static_cast<socklen_t>(sizeof(struct sockaddr_in));
      return length;
    }
    case AF_INET6: {
      constexpr socklen_t length = static_cast<socklen_t>(sizeof(struct sockaddr_in6));
      return length;
    }
    default:
      return 0;
  }
}

bool operator==(const NetworkAddress &l, const NetworkAddress &r) {
  return std::forward_as_tuple(l.Ip(), l.Port()) == std::forward_as_tuple(r.Ip(), r.Port());
}

bool operator!=(const NetworkAddress &l, const NetworkAddress &r) { return !(l == r); }

bool operator<(const NetworkAddress &l, const NetworkAddress &r) {
  return std::forward_as_tuple(l.Ip(), l.Port()) < std::forward_as_tuple(r.Ip(), r.Port());
}

}  // namespace polaris
