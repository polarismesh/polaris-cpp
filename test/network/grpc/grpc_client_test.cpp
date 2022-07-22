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

#include "network/grpc/client.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <google/protobuf/wrappers.pb.h>

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

  virtual void TearDown() { reactor_.Stop(); }

 protected:
  std::string host_;
  int port_;
  Reactor reactor_;
};

void ConnectCheck(ReturnCode expect_return_code, ReturnCode real_return_code) {
  ASSERT_EQ(expect_return_code, real_return_code);
}

class StreamCb : public GrpcStreamCallback {
 public:
  virtual ~StreamCb() {}

  virtual bool OnReceiveResponse(Buffer* /*response*/) {
    EXPECT_FALSE(true);
    return false;
  }

  virtual void OnRemoteClose(const std::string& message) { ASSERT_TRUE(!message.empty()); }
};

TEST_F(GrpcClientTest, SyncConnectToNotExistServer) {
  GrpcClient grpc_client(reactor_);
  ASSERT_TRUE(grpc_client.ConnectTo(host_, port_));
  ASSERT_FALSE(grpc_client.WaitConnected(100));
}

TEST_F(GrpcClientTest, AsyncConnectToNotExistServer) {
  GrpcClient grpc_client(reactor_);
  grpc_client.Connect(host_, port_, 100, std::bind(ConnectCheck, kReturnNetworkFailed, std::placeholders::_1));
}

TEST_F(GrpcClientTest, SyncConnectToErrorServer) {
  NetServerParam param(port_, "abc", kNetServerInit, 0);
  int rc = pthread_create(&param.tid_, nullptr, FakeNetServer::StartTcp, &param);
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
  pthread_join(param.tid_, nullptr);
}

TEST_F(GrpcClientTest, AsyncConnectToErrorServer) {
  NetServerParam param(port_, "abc", kNetServerInit, 0);
  int rc = pthread_create(&param.tid_, nullptr, FakeNetServer::StartTcp, &param);
  ASSERT_EQ(rc, 0);
  while (param.status_ == kNetServerInit) {
    usleep(2000);
  }
  ASSERT_EQ(param.status_, kNetServerStart);
  do {
    GrpcClient grpc_client(reactor_);
    grpc_client.Connect(host_, port_, 100, std::bind(ConnectCheck, kReturnOk, std::placeholders::_1));
    StreamCb stream_cb;
    grpc_client.StartStream("hello", stream_cb);
  } while (false);  // 确保grpc_client析构
  reactor_.RunOnce();
  param.status_ = kNetServerStop;
  pthread_join(param.tid_, nullptr);
}

class MockStreamCallback : public StreamCallback<google::protobuf::StringValue> {
 public:
  explicit MockStreamCallback(GrpcClient* grpc_client) : grpc_client_(grpc_client) {}
  virtual ~MockStreamCallback() {}

  MOCK_METHOD1(OnReceiveMessage, void(google::protobuf::StringValue* message));
  MOCK_METHOD1(OnRemoteClose, void(const std::string& message));

  void DeleteClinet() { delete grpc_client_; }

 private:
  GrpcClient* grpc_client_;
};

TEST_F(GrpcClientTest, TestHttpStreamCallback) {
  // Test OnDate
  do {
    GrpcClient* grpc_client = new GrpcClient(reactor_);
    MockStreamCallback stream_callback(grpc_client);
    EXPECT_CALL(stream_callback, OnRemoteClose("decode http2 data frame to grpc data error"))
        .WillOnce(::testing::InvokeWithoutArgs(&stream_callback, &MockStreamCallback::DeleteClinet));
    GrpcStream* stream = grpc_client->StartStream("hello", stream_callback);
    Buffer buffer;
    buffer.Add("hello", 2);
    stream->OnData(buffer, false);
  } while (false);

  do {
    GrpcClient* grpc_client = new GrpcClient(reactor_);
    MockStreamCallback stream_callback(grpc_client);
    EXPECT_CALL(stream_callback, OnRemoteClose("decode grpc data to pb message error"))
        .WillOnce(::testing::InvokeWithoutArgs(&stream_callback, &MockStreamCallback::DeleteClinet));
    GrpcStream* stream = grpc_client->StartStream("hello", stream_callback);
    Buffer buffer;
    uint8_t length[1 + 4 + 2] = {0x0, 0x0, 0x0, 0x0, 0x2, 0x3, 0x8};
    buffer.Add(&length, 7);
    stream->OnData(buffer, false);
  } while (false);

  reactor_.Stop();
}

}  // namespace grpc
}  // namespace polaris
