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

#include "utils/netclient.h"

#include <gtest/gtest.h>

#include <pthread.h>

#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "logger.h"
#include "mock/fake_net_server.h"
#include "test_utils.h"

namespace polaris {

class NetClientTest : public ::testing::Test {
 protected:
  static void SetUpTestCase() {
    tcp_server_.port_ = TestUtils::PickUnusedPort();
    tcp_server_.response_ = std::string("HTTP/1.0 200 OK\r\n\r\n");
    tcp_server_.status_ = kNetServerInit;
    pthread_create(&tcp_server_.tid_, nullptr, FakeNetServer::StartTcp, &tcp_server_);

    udp_server_.port_ = TestUtils::PickUnusedPort();
    udp_server_.response_ = std::string("0x12345678");
    udp_server_.status_ = kNetServerInit;
    pthread_create(&udp_server_.tid_, nullptr, FakeNetServer::StartUdp, &udp_server_);

    while (tcp_server_.status_ == kNetServerInit || udp_server_.status_ == kNetServerInit) {
      usleep(100);
    }
    ASSERT_EQ(tcp_server_.status_, kNetServerStart);
    ASSERT_EQ(udp_server_.status_, kNetServerStart);
  }

  static void TearDownTestCase() {
    tcp_server_.status_ = kNetServerStop;
    pthread_join(tcp_server_.tid_, nullptr);
    udp_server_.status_ = kNetServerStop;
    pthread_join(udp_server_.tid_, nullptr);
  }
  static NetServerParam tcp_server_;
  static NetServerParam udp_server_;
};

NetServerParam NetClientTest::tcp_server_;
NetServerParam NetClientTest::udp_server_;

TEST_F(NetClientTest, TcpSendRecv) {
  sleep(1);
  int port = tcp_server_.port_;
  int timeout_ms = 100;
  std::string host = "127.0.0.1";
  std::string response_package;
  std::string request_package = "GET /health HTTP/1.0\r\n\r\n";
  int retcode = 0;

  retcode = NetClient::TcpSendRecv(host, port, timeout_ms, request_package, &response_package);
  ASSERT_EQ(retcode, 0);

  // host 为0.0.0.0
  host = "0.0.0.0";
  retcode = NetClient::TcpSendRecv(host, port, timeout_ms, request_package, &response_package);
  ASSERT_EQ(retcode, 0);

  // host错误
  host = "2.3.4.5";
  retcode = NetClient::TcpSendRecv(host, port, timeout_ms, request_package, &response_package);
  ASSERT_EQ(retcode, -1);

  // port 错误
  host = "0.0.0.0";
  port = TestUtils::PickUnusedPort();
  retcode = NetClient::TcpSendRecv(host, port, timeout_ms, request_package, &response_package);
  ASSERT_EQ(retcode, -1);

  retcode = NetClient::TcpSendRecv(host, port, timeout_ms, request_package, nullptr);
  ASSERT_EQ(retcode, -1);

  // timout
  host = "0.0.0.0";
  port = tcp_server_.port_;
  timeout_ms = 3;
  retcode = NetClient::TcpSendRecv(host, port, timeout_ms, request_package, &response_package);
  ASSERT_EQ(retcode, -1);

  timeout_ms = 100;
  retcode = NetClient::TcpSendRecv(host, port, timeout_ms, request_package, &response_package);
  ASSERT_EQ(retcode, 0);

  // 连接测试
  timeout_ms = 10;
  retcode = NetClient::TcpSendRecv(host, port, timeout_ms, "", &response_package);
  ASSERT_EQ(retcode, 0);

  // 只发不收
  retcode = NetClient::TcpSendRecv(host, port, timeout_ms, request_package, nullptr);
  ASSERT_EQ(retcode, 0);

  // 一发一收
  retcode = NetClient::TcpSendRecv(host, port, timeout_ms, request_package, &response_package);
  ASSERT_EQ(retcode, -1);
}

TEST_F(NetClientTest, UdpSendRecv) {
  int port = udp_server_.port_;
  int timeout_ms = 100;
  std::string host = "127.0.0.1";
  std::string response_package;
  std::string request_package = "GET /health HTTP/1.0\r\n\r\n";
  int retcode = 0;

  retcode = NetClient::UdpSendRecv(host, port, timeout_ms, request_package, &response_package);
  // POLARIS_LOG(LOG_INFO, "UdpSendRecv response_package = %s",
  // response_package.c_str());
  ASSERT_EQ(retcode, 0);

  // host 为0.0.0.0
  host = "0.0.0.0";
  retcode = NetClient::UdpSendRecv(host, port, timeout_ms, request_package, &response_package);
  ASSERT_EQ(retcode, 0);

  // host错误
  host = "2.3.4.5";
  retcode = NetClient::UdpSendRecv(host, port, timeout_ms, request_package, &response_package);
  ASSERT_EQ(retcode, -1);

  // port 错误
  host = "0.0.0.0";
  port = TestUtils::PickUnusedPort();
  retcode = NetClient::UdpSendRecv(host, port, timeout_ms, request_package, &response_package);
  ASSERT_EQ(retcode, -1);

  retcode = NetClient::UdpSendRecv(host, port, timeout_ms, request_package, nullptr);
  ASSERT_EQ(retcode, -1);

  // timout
  host = "0.0.0.0";
  port = udp_server_.port_;
  timeout_ms = 3;
  retcode = NetClient::UdpSendRecv(host, port, timeout_ms, request_package, &response_package);
  ASSERT_EQ(retcode, -1);

  timeout_ms = 100;
  retcode = NetClient::UdpSendRecv(host, port, timeout_ms, request_package, &response_package);
  ASSERT_EQ(retcode, 0);

  timeout_ms = 10;
  retcode = NetClient::UdpSendRecv(host, port, timeout_ms, "", &response_package);
  ASSERT_EQ(retcode, -1);

  retcode = NetClient::UdpSendRecv(host, port, timeout_ms, request_package, nullptr);
  ASSERT_EQ(retcode, -1);

  // 一发一收
  retcode = NetClient::UdpSendRecv(host, port, timeout_ms, request_package, &response_package);
  ASSERT_EQ(retcode, -1);
}

}  // namespace polaris
