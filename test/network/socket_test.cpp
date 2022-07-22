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

#include "network/socket.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <ifaddrs.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <memory>
#include <random>
#include <thread>

namespace polaris {

class MockSocket : public Socket {};

static bool ipv6_enable = false;

class SocketTest : public ::testing::Test {
 protected:
  static void SetUpTestCase() {
    ifaddrs *ifa = nullptr;
    getifaddrs(&ifa);

    for (ifaddrs *ifp = ifa; ifp != nullptr; ifp = ifp->ifa_next) {
      if (!ifp->ifa_addr) {
        continue;
      }
      if (ifa->ifa_addr->sa_family == AF_INET6) {
        ipv6_enable = true;
      }
    }
    if (ifa != nullptr) {
      freeifaddrs(ifa);
    }
  }

  void SetUp() override {
    tcp_ipv4_server_sock_.reset(new Socket(Socket::CreateTcpSocket(false)));
    tcp_ipv4_client_sock_.reset(new Socket(Socket::CreateTcpSocket(false)));

    if (ipv6_enable) {
      tcp_ipv6_server_sock_.reset(new Socket(Socket::CreateTcpSocket(true)));
      tcp_ipv6_client_sock_.reset(new Socket(Socket::CreateTcpSocket(true)));
    }
  }

 protected:
  std::unique_ptr<Socket> tcp_ipv4_server_sock_;
  std::unique_ptr<Socket> tcp_ipv4_client_sock_;

  std::unique_ptr<Socket> tcp_ipv6_server_sock_;
  std::unique_ptr<Socket> tcp_ipv6_client_sock_;
};

TEST_F(SocketTest, GetFd) {
  EXPECT_GE(tcp_ipv4_server_sock_->GetFd(), 0);

  if (ipv6_enable) {
    EXPECT_GE(tcp_ipv6_server_sock_->GetFd(), 0);
  }
}

TEST_F(SocketTest, IsValid) {
  EXPECT_TRUE(tcp_ipv4_server_sock_->IsValid());

  if (ipv6_enable) {
    EXPECT_TRUE(tcp_ipv6_server_sock_->IsValid());
  }
}

TEST_F(SocketTest, Close) {
  tcp_ipv4_server_sock_->Close();
  EXPECT_FALSE(tcp_ipv4_server_sock_->IsValid());
  EXPECT_EQ(-1, tcp_ipv4_server_sock_->GetFd());
}

TEST_F(SocketTest, ReuseAddr) {
  tcp_ipv4_server_sock_->SetReuseAddr();
  int opt = 0;
  socklen_t opt_len = static_cast<socklen_t>(sizeof(opt));
  tcp_ipv4_server_sock_->GetSockOpt(SO_REUSEADDR, &opt, &opt_len, SOL_SOCKET);
  EXPECT_EQ(1, opt);
}

TEST_F(SocketTest, SetBlock) {
  tcp_ipv4_server_sock_->SetBlock(true);
  tcp_ipv4_server_sock_->SetBlock(false);
}

TEST_F(SocketTest, CloseWait) {
  tcp_ipv4_server_sock_->SetNoCloseWait();
  tcp_ipv4_server_sock_->SetCloseWait(30);
  tcp_ipv4_server_sock_->SetCloseWaitDefault();
}

TEST_F(SocketTest, TcpNoDelay) {
  tcp_ipv4_server_sock_->SetTcpNoDelay();
  int opt = 0;
  socklen_t opt_len = static_cast<socklen_t>(sizeof(opt));
  tcp_ipv4_server_sock_->GetSockOpt(TCP_NODELAY, &opt, &opt_len, IPPROTO_TCP);
  EXPECT_EQ(1, opt);
}

TEST_F(SocketTest, KeepAlive) {
  tcp_ipv4_client_sock_->SetKeepAlive();
  int opt = 0;
  socklen_t opt_len = static_cast<socklen_t>(sizeof(opt));
  tcp_ipv4_client_sock_->GetSockOpt(SO_KEEPALIVE, &opt, &opt_len, SOL_SOCKET);
  EXPECT_EQ(1, opt);
}

TEST_F(SocketTest, SendBufferSize) {
  // The kernel will double this value. see `man 7 socket`
  tcp_ipv4_server_sock_->SetSendBufferSize(10240);
  EXPECT_EQ(2 * 10240, tcp_ipv4_server_sock_->GetSendBufferSize());
}

TEST_F(SocketTest, RecvBuffeSize) {
  // The kernel will double this value. see `man 7 socket`
  tcp_ipv4_server_sock_->SetRecvBufferSize(10240);
  EXPECT_EQ(2 * 10240, tcp_ipv4_server_sock_->GetRecvBufferSize());
}

TEST_F(SocketTest, TcpCommunicattion) {
  char message[] = "helloworld";

  {
    std::random_device rd;
    std::uniform_int_distribution<uint16_t> dist(30000, 60000);
    int random_port = dist(rd);

    NetworkAddress addr = NetworkAddress(random_port, false);
    tcp_ipv4_server_sock_->SetReuseAddr();
    tcp_ipv4_server_sock_->Bind(addr);
    tcp_ipv4_server_sock_->Listen();

    std::thread t1([&]() {
      sleep(1);
      NetworkAddress addr;
      int conn_fd = tcp_ipv4_server_sock_->Accept(&addr);
      Socket sock(conn_fd);

      char recvbuf[1024] = {0};
      sock.Recv(recvbuf, sizeof(recvbuf));

      EXPECT_STREQ(message, recvbuf);
    });

    tcp_ipv4_client_sock_->Connect(addr);
    tcp_ipv4_client_sock_->Send(message, sizeof(message));

    t1.join();
  }

  if (ipv6_enable) {
    NetworkAddress addr = NetworkAddress(10003, false, true);
    tcp_ipv4_server_sock_->SetReuseAddr();
    tcp_ipv6_server_sock_->Bind(addr);
    tcp_ipv6_server_sock_->Listen();

    std::thread t1([&]() {
      sleep(1);
      NetworkAddress addr;
      int conn_fd = tcp_ipv6_server_sock_->Accept(&addr);
      Socket sock(conn_fd);

      char recvbuf[1024] = {0};
      sock.Recv(recvbuf, sizeof(recvbuf), 0);

      EXPECT_STREQ(message, recvbuf);
    });

    tcp_ipv6_client_sock_->Connect(addr);
    tcp_ipv6_client_sock_->Send(message, sizeof(message));
  }
}

}  // namespace polaris
