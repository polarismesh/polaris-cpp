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

#include "grpc/client.h"

#include <gtest/gtest.h>

#include "mock/fake_net_server.h"
#include "reactor/reactor.h"
#include "test_utils.h"

namespace polaris {
namespace grpc {

class GrpcClientTest : public ::testing::Test {
protected:
  virtual void SetUp() {
    host_ = "127.0.0.1";
    port_ = TestUtils::PickUnusedPort();
  }

  virtual void TearDown() {}

protected:
  std::string host_;
  int port_;
  Reactor reactor_;
};

// 异步连接回调
class ConnectionCb : public ConnectCallback {
public:
  explicit ConnectionCb(bool expect_result) : expect_result_(expect_result) {}
  virtual ~ConnectionCb() {}

  virtual void OnSuccess() { ASSERT_EQ(expect_result_, true); }

  virtual void OnFailed() { ASSERT_EQ(expect_result_, false); }

  virtual void OnTimeout() { ASSERT_EQ(expect_result_, false); }

private:
  bool expect_result_;
};

class StreamCb : public GrpcStreamCallback {
public:
  virtual ~StreamCb() {}

  virtual bool OnReceiveMessage(Buffer * /*response*/) {
    EXPECT_FALSE(true);
    return false;
  }

  virtual void OnRemoteClose(GrpcStatusCode status, const std::string &message) {
    ASSERT_EQ(status, kGrpcStatusUnavailable);
    ASSERT_TRUE(!message.empty());
  }
};

TEST_F(GrpcClientTest, SyncConnectToNotExistServer) {
  GrpcClient grpc_client(reactor_);
  ASSERT_TRUE(grpc_client.ConnectTo(host_, port_));
  ASSERT_FALSE(grpc_client.WaitConnected(100));
}

TEST_F(GrpcClientTest, AsyncConnectToNotExistServer) {
  GrpcClient grpc_client(reactor_);
  grpc_client.ConnectTo(host_, port_, 100, new ConnectionCb(false));
}

TEST_F(GrpcClientTest, SyncConnectToErrorServer) {
  NetServerParam param(port_, "abc", kNetServerInit, 0);
  int rc = pthread_create(&param.tid_, NULL, FakeNetServer::StartTcp, &param);
  ASSERT_EQ(rc, 0);
  while (param.status_ == kNetServerInit) {
    usleep(2000);
  }
  ASSERT_EQ(param.status_, kNetServerStart);
  do {
    GrpcClient grpc_client(reactor_);
    ASSERT_TRUE(grpc_client.ConnectTo(host_, port_));
    ASSERT_TRUE(grpc_client.WaitConnected(100));
    grpc_client.SubmitToReactor();
    StreamCb stream_cb;
    grpc_client.StartStream("hello", stream_cb);
  } while (false);  // 确保grpc_client析构
  reactor_.RunOnce();
  param.status_ = kNetServerStop;
  pthread_join(param.tid_, NULL);
  reactor_.Stop();
}

TEST_F(GrpcClientTest, AsyncConnectToErrorServer) {
  NetServerParam param(port_, "abc", kNetServerInit, 0);
  int rc = pthread_create(&param.tid_, NULL, FakeNetServer::StartTcp, &param);
  ASSERT_EQ(rc, 0);
  while (param.status_ == kNetServerInit) {
    usleep(2000);
  }
  ASSERT_EQ(param.status_, kNetServerStart);
  do {
    GrpcClient grpc_client(reactor_);
    grpc_client.ConnectTo(host_, port_, 100, new ConnectionCb(true));
    StreamCb stream_cb;
    grpc_client.StartStream("hello", stream_cb);
  } while (false);  // 确保grpc_client析构
  reactor_.RunOnce();
  param.status_ = kNetServerStop;
  pthread_join(param.tid_, NULL);
  reactor_.Stop();
}

}  // namespace grpc
}  // namespace polaris
