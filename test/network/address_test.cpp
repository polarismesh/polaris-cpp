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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <vector>

namespace polaris {

const uint16_t port = 1357;

const char ipv4_ip[] = "1.2.3.4";
const char ipv4_ip_port[] = "1.2.3.4:1357";

const char ipv6_ip[] = "1:2:3:4:5:6:7:8";
const char ipv6_ip_port[] = "[1:2:3:4:5:6:7:8]:1357";

const char ipv4_loopback_ip[] = "127.0.0.1";
const char ipv4_loopback_ip_port[] = "127.0.0.1:1357";

const char ipv4_any_ip[] = "0.0.0.0";
const char ipv4_any_ip_port[] = "0.0.0.0:1357";

const char ipv6_loopback_ip[] = "::1";
const char ipv6_loopback_ip_port[] = "[::1]:1357";

const char ipv6_any_ip[] = "::";
const char ipv6_any_ip_port[] = "[::]:1357";

class NetworkAddressTest : public ::testing::Test {
 protected:
  void SetUp() override {
    unknown_addrs_.push_back(NetworkAddress());

    struct sockaddr_in sockaddr4;
    sockaddr4.sin_family = AF_INET;
    sockaddr4.sin_addr.s_addr = inet_addr(ipv4_ip);
    sockaddr4.sin_port = htons(port);
    ipv4_addrs_.push_back(NetworkAddress(reinterpret_cast<struct sockaddr *>(&sockaddr4)));
    ipv4_addrs_.push_back(NetworkAddress(ipv4_ip, port));
    ipv4_addrs_.push_back(NetworkAddress(ipv4_ip_port));

    struct sockaddr_in6 sockaddr6;
    sockaddr6.sin6_family = AF_INET6;
    inet_pton(AF_INET6, ipv6_ip, &sockaddr6.sin6_addr);
    sockaddr6.sin6_port = htons(1357);
    ipv6_addrs_.push_back(NetworkAddress(reinterpret_cast<struct sockaddr *>(&sockaddr6)));
    ipv6_addrs_.push_back(NetworkAddress(ipv6_ip, port));
    ipv6_addrs_.push_back(NetworkAddress(ipv6_ip_port));

    ipv4_loopback_addr_ = NetworkAddress(port, true);
    ipv4_any_addr_ = NetworkAddress(port, false);

    ipv6_loopback_addr_ = NetworkAddress(port, true, true);
    ipv6_any_addr_ = NetworkAddress(port, false, true);
  };

 protected:
  std::vector<NetworkAddress> unknown_addrs_;
  std::vector<NetworkAddress> ipv4_addrs_;
  std::vector<NetworkAddress> ipv6_addrs_;
  NetworkAddress ipv4_loopback_addr_;
  NetworkAddress ipv4_any_addr_;
  NetworkAddress ipv6_loopback_addr_;
  NetworkAddress ipv6_any_addr_;
};

TEST_F(NetworkAddressTest, IsValid) {
  for (auto const &addr : unknown_addrs_) {
    EXPECT_FALSE(addr.IsValid());
  }

  for (auto const &addr : ipv4_addrs_) {
    EXPECT_TRUE(addr.IsValid());
  }

  for (auto const &addr : ipv6_addrs_) {
    EXPECT_TRUE(addr.IsValid());
  }

  EXPECT_TRUE(ipv4_loopback_addr_.IsValid());
  EXPECT_TRUE(ipv4_any_addr_.IsValid());
  EXPECT_TRUE(ipv6_loopback_addr_.IsValid());
  EXPECT_TRUE(ipv6_any_addr_.IsValid());
}

TEST_F(NetworkAddressTest, ToString) {
  for (auto const &addr : unknown_addrs_) {
    EXPECT_EQ("", addr.ToString());
  }

  for (auto const &addr : ipv4_addrs_) {
    EXPECT_EQ(ipv4_ip_port, addr.ToString());
  }

  for (auto const &addr : ipv6_addrs_) {
    EXPECT_EQ(ipv6_ip_port, addr.ToString());
  }

  EXPECT_EQ(ipv4_loopback_ip_port, ipv4_loopback_addr_.ToString());
  EXPECT_EQ(ipv4_any_ip_port, ipv4_any_addr_.ToString());

  EXPECT_EQ(ipv6_loopback_ip_port, ipv6_loopback_addr_.ToString());
  EXPECT_EQ(ipv6_any_ip_port, ipv6_any_addr_.ToString());
}

TEST_F(NetworkAddressTest, Ip) {
  for (auto const &addr : unknown_addrs_) {
    EXPECT_EQ("", addr.Ip());
  }

  for (auto const &addr : ipv4_addrs_) {
    EXPECT_EQ(ipv4_ip, addr.Ip());
  }

  for (auto const &addr : ipv6_addrs_) {
    EXPECT_EQ(ipv6_ip, addr.Ip());
  }

  EXPECT_EQ(ipv4_loopback_ip, ipv4_loopback_addr_.Ip());
  EXPECT_EQ(ipv4_any_ip, ipv4_any_addr_.Ip());

  EXPECT_EQ(ipv6_loopback_ip, ipv6_loopback_addr_.Ip());
  EXPECT_EQ(ipv6_any_ip, ipv6_any_addr_.Ip());
}

TEST_F(NetworkAddressTest, Port) {
  for (auto const &addr : unknown_addrs_) {
    EXPECT_EQ(0, addr.Port());
  }

  for (auto const &addr : ipv4_addrs_) {
    EXPECT_EQ(port, addr.Port());
  }

  for (auto const &addr : ipv6_addrs_) {
    EXPECT_EQ(port, addr.Port());
  }

  EXPECT_EQ(port, ipv4_loopback_addr_.Port());
  EXPECT_EQ(port, ipv4_any_addr_.Port());

  EXPECT_EQ(port, ipv6_loopback_addr_.Port());
  EXPECT_EQ(port, ipv6_any_addr_.Port());
}

TEST_F(NetworkAddressTest, Family) {
  for (auto const &addr : unknown_addrs_) {
    EXPECT_EQ(AF_UNSPEC, addr.Family());
  }

  for (auto const &addr : ipv4_addrs_) {
    EXPECT_EQ(AF_INET, addr.Family());
  }

  for (auto const &addr : ipv6_addrs_) {
    EXPECT_EQ(AF_INET6, addr.Family());
  }

  EXPECT_EQ(AF_INET, ipv4_loopback_addr_.Family());
  EXPECT_EQ(AF_INET, ipv4_any_addr_.Family());

  EXPECT_EQ(AF_INET6, ipv6_loopback_addr_.Family());
  EXPECT_EQ(AF_INET6, ipv6_any_addr_.Family());
}

TEST_F(NetworkAddressTest, IsIpv4) {
  for (auto const &addr : unknown_addrs_) {
    EXPECT_FALSE(addr.IsIpv4());
  }

  for (auto const &addr : ipv4_addrs_) {
    EXPECT_TRUE(addr.IsIpv4());
  }

  for (auto const &addr : ipv6_addrs_) {
    EXPECT_FALSE(addr.IsIpv4());
  }

  EXPECT_TRUE(ipv4_loopback_addr_.IsIpv4());
  EXPECT_TRUE(ipv4_any_addr_.IsIpv4());

  EXPECT_FALSE(ipv6_loopback_addr_.IsIpv4());
  EXPECT_FALSE(ipv6_any_addr_.IsIpv4());
}

TEST_F(NetworkAddressTest, IsIpv6) {
  for (auto const &addr : unknown_addrs_) {
    EXPECT_FALSE(addr.IsIpv6());
  }

  for (auto const &addr : ipv4_addrs_) {
    EXPECT_FALSE(addr.IsIpv6());
  }

  for (auto const &addr : ipv6_addrs_) {
    EXPECT_TRUE(addr.IsIpv6());
  }

  EXPECT_FALSE(ipv4_loopback_addr_.IsIpv6());
  EXPECT_FALSE(ipv4_any_addr_.IsIpv6());

  EXPECT_TRUE(ipv6_loopback_addr_.IsIpv6());
  EXPECT_TRUE(ipv6_any_addr_.IsIpv6());
}

TEST_F(NetworkAddressTest, Sockaddr) {
  for (auto const &addr : unknown_addrs_) {
    EXPECT_EQ(nullptr, addr.Sockaddr());
  }

  for (auto const &addr : ipv4_addrs_) {
    EXPECT_TRUE(nullptr != addr.Sockaddr());
  }

  for (auto const &addr : ipv6_addrs_) {
    EXPECT_TRUE(nullptr != addr.Sockaddr());
  }

  EXPECT_TRUE(nullptr != ipv4_loopback_addr_.Sockaddr());
  EXPECT_TRUE(nullptr != ipv4_any_addr_.Sockaddr());

  EXPECT_TRUE(nullptr != ipv6_loopback_addr_.Sockaddr());
  EXPECT_TRUE(nullptr != ipv6_any_addr_.Sockaddr());
}

TEST_F(NetworkAddressTest, Socklen) {
  for (auto const &addr : ipv4_addrs_) {
    EXPECT_EQ(sizeof(struct sockaddr_in), addr.Socklen());
  }

  for (auto const &addr : ipv6_addrs_) {
    EXPECT_EQ(sizeof(struct sockaddr_in6), addr.Socklen());
  }

  EXPECT_EQ(sizeof(struct sockaddr_in), ipv4_loopback_addr_.Socklen());
  EXPECT_EQ(sizeof(struct sockaddr_in), ipv4_any_addr_.Socklen());

  EXPECT_EQ(sizeof(struct sockaddr_in6), ipv6_loopback_addr_.Socklen());
  EXPECT_EQ(sizeof(struct sockaddr_in6), ipv6_any_addr_.Socklen());
}

TEST_F(NetworkAddressTest, Boolean) {
  for (auto const &addr : unknown_addrs_) {
    EXPECT_FALSE(addr);
  }

  for (auto const &addr : ipv4_addrs_) {
    EXPECT_TRUE(addr);
  }

  for (auto const &addr : ipv6_addrs_) {
    EXPECT_TRUE(addr);
  }

  EXPECT_TRUE(ipv4_loopback_addr_);
  EXPECT_TRUE(ipv4_any_addr_);

  EXPECT_TRUE(ipv6_loopback_addr_);
  EXPECT_TRUE(ipv6_any_addr_);
}

TEST_F(NetworkAddressTest, Compare) {
  EXPECT_TRUE(unknown_addrs_[0] != ipv4_addrs_[0]);
  EXPECT_TRUE(unknown_addrs_[0] != ipv6_addrs_[0]);
  EXPECT_TRUE(ipv4_addrs_[0] != ipv6_addrs_[0]);
  EXPECT_TRUE(ipv4_addrs_[0] == ipv4_addrs_[1]);
  EXPECT_TRUE(ipv6_addrs_[0] == ipv6_addrs_[1]);
  EXPECT_TRUE(unknown_addrs_[0] < ipv4_addrs_[0]);
}

}  // namespace polaris
