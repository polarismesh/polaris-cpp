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

#include "plugin/server_connector/grpc_server_connector.h"

#include <gmock/gmock.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "network/grpc/client.h"
#include "polaris/provider.h"
#include "test_context.h"
#include "test_utils.h"
#include "v1/code.pb.h"

namespace polaris {

class MockGrpcClient : public grpc::GrpcClient {
 public:
  explicit MockGrpcClient(Reactor &reactor) : grpc::GrpcClient(reactor) {}
  MOCK_METHOD0(SubmitToReactor, void());
  MOCK_METHOD4(SendRequest, grpc::GrpcStream *(google::protobuf::Message &request, const std::string &call_path,
                                               uint64_t timeout, grpc::GrpcRequestCallback &callback));
};

class BlockRequestForTest : public BlockRequest {
 public:
  BlockRequestForTest(PolarisRequestType request_type, GrpcServerConnector &connector, uint64_t timeout)
      : BlockRequest(request_type, connector, timeout), connector_(connector) {}

  virtual bool PrepareClient() {
    grpc_client_ = mock_grpc_client_;
    instance_ = new Instance("id", "127.0.0.1", 8081, 100);
    return true;
  }

  void SetupExpectCall(ReturnCode return_code, v1::Response *response) {
    return_code_ = return_code;
    response_ = response;
    mock_grpc_client_ = new MockGrpcClient(connector_.GetReactor());
    EXPECT_CALL(*mock_grpc_client_, SubmitToReactor()).Times(1);
    EXPECT_CALL(*mock_grpc_client_, SendRequest(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(this, &BlockRequestForTest::Callback));
  }

  grpc::GrpcStream *Callback(google::protobuf::Message &, const std::string &, uint64_t,
                             grpc::GrpcRequestCallback &callback) {
    if (return_code_ == kReturnOk) {
      Buffer *body = new Buffer();
      const size_t size = response_->ByteSizeLong();
      RawSlice iovec;
      body->Reserve(size, &iovec, 1);
      iovec.len_ = size;
      uint8_t *current = reinterpret_cast<uint8_t *>(iovec.mem_);
      google::protobuf::io::ArrayOutputStream stream(current, size, -1);
      google::protobuf::io::CodedOutputStream codec_stream(&stream);
      response_->SerializeWithCachedSizes(&codec_stream);
      body->Commit(&iovec, 1);
      callback.OnResponse(body);
      delete response_;
    } else {
      callback.OnFailure("grpc error");
    }
    return nullptr;
  }

 private:
  MockGrpcClient *mock_grpc_client_;
  GrpcServerConnector &connector_;
  ReturnCode return_code_;
  v1::Response *response_;
};

class GrpcServerConnectorForTest : public GrpcServerConnector {
 public:
  GrpcServerConnectorForTest() : fake_(false) { discover_stream_state_ = kDiscoverStreamGetInstance; }

  virtual BlockRequest *CreateBlockRequest(PolarisRequestType request_type, uint64_t timeout) {
    const std::lock_guard<std::mutex> guard(lock_);
    if (fake_) {
      BlockRequestForTest *request = new BlockRequestForTest(request_type, *this, timeout);
      request->SetupExpectCall(return_code_, response_);
      fake_ = false;
      return request;
    } else {
      return GrpcServerConnector::CreateBlockRequest(request_type, timeout);
    }
  }

  virtual ReturnCode SelectInstance(const ServiceKey & /*service_key*/, uint32_t /*timeout*/, Instance **instance,
                                    bool /*ignore_half_open*/) {
    *instance = new Instance("id", "127.0.0.1", 8081, 100);
    return kReturnOk;
  }

  void SetupExpect(ReturnCode return_code, v1::Response *response) {
    const std::lock_guard<std::mutex> guard(lock_);
    fake_ = true;
    return_code_ = return_code;
    response_ = response;
  }

 private:
  std::mutex lock_;
  bool fake_;
  ReturnCode return_code_;
  v1::Response *response_;
};

class GrpcServerConnectorTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    // default service
    service_namespace_ = "cpp_test_namespace";
    service_name_ = "cpp_test_service";
    service_token_ = "cpp_test_token";
    // create client
    context_ = TestContext::CreateContext();
    std::string err_msg;
    std::string content = "addresses: [127.0.0.1:" + std::to_string(TestUtils::PickUnusedPort()) + "]";
    Config *config = Config::CreateFromString(content, err_msg);
    POLARIS_ASSERT(config != nullptr && err_msg.empty());
    server_connector = new GrpcServerConnectorForTest();
    server_connector->Init(config, context_);
    delete config;
  }

  virtual void TearDown() {
    if (server_connector != nullptr) {
      delete server_connector;
    }
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
  }

  v1::Response *CreateResponse(v1::RetCode code, const std::string instance_id = "instance_id") {
    v1::Response *response = new v1::Response();
    response->mutable_code()->set_value(code);
    response->mutable_instance()->mutable_id()->set_value(instance_id);
    return response;
  }

 protected:
  Context *context_;
  GrpcServerConnectorForTest *server_connector;

  std::string service_namespace_;
  std::string service_name_;
  std::string service_token_;
};

// 测试服务revision变更逻辑
TEST_F(GrpcServerConnectorTest, TestUpdateRevision) {
  ServiceListener listener;
  uint64_t last_version = 0;
  listener.cache_version_ = 0;
  listener.ret_code_ = 0;

  for (int i = 0; i < 10; ++i) {
    // 服务不存在
    ::v1::DiscoverResponse response;
    response.mutable_code()->set_value(v1::NotFoundService);
    // 服务端在服务不存在时会返回客户端请求里的revision
    response.mutable_service()->mutable_revision()->set_value("123456");
    ASSERT_TRUE(server_connector->UpdateRevision(listener, response));
    ASSERT_TRUE(listener.revision_.empty());
    ASSERT_TRUE(listener.ret_code_ == v1::NotFoundService);
    ASSERT_EQ(listener.cache_version_, ++last_version);

    // 变了一个错误码
    response.mutable_code()->set_value(v1::NotFoundResource);
    ASSERT_TRUE(server_connector->UpdateRevision(listener, response));
    ASSERT_TRUE(listener.revision_.empty());
    ASSERT_TRUE(listener.ret_code_ == v1::NotFoundResource);
    ASSERT_EQ(listener.cache_version_, ++last_version);

    // 服务实例连续正常返回变更
    for (int ok = 0; ok < 10; ok++) {
      response.mutable_code()->set_value(v1::ExecuteSuccess);
      response.mutable_service()->mutable_revision()->set_value("ok" + std::to_string(ok));
      ASSERT_TRUE(server_connector->UpdateRevision(listener, response));
      ASSERT_TRUE(listener.ret_code_ == v1::ExecuteSuccess);
      ASSERT_EQ(listener.cache_version_, ++last_version);
      ASSERT_EQ(listener.revision_, response.service().revision().value());
    }

    // 服务实例正常返回不变化
    for (int ok = 0; ok < 10; ok++) {
      response.mutable_code()->set_value(v1::DataNoChange);
      response.mutable_service()->mutable_revision()->set_value(listener.revision_);
      ASSERT_FALSE(server_connector->UpdateRevision(listener, response));
      ASSERT_TRUE(listener.ret_code_ == v1::DataNoChange);
      ASSERT_EQ(listener.cache_version_, last_version);
      ASSERT_EQ(listener.revision_, response.service().revision().value());
    }

    // 模拟路由规则未写配置的情况返回
    last_version++;
    for (int ok = 0; ok < 10; ok++) {
      response.mutable_code()->set_value(v1::ExecuteSuccess);
      response.mutable_service()->mutable_revision()->clear_value();
      ASSERT_EQ(server_connector->UpdateRevision(listener, response), ok == 0);
      ASSERT_TRUE(listener.ret_code_ == v1::ExecuteSuccess);
      ASSERT_EQ(listener.cache_version_, last_version);
      ASSERT_TRUE(listener.revision_.empty());
    }
  }
}

// 服务注册
TEST_F(GrpcServerConnectorTest, RegisterInstance) {
  InstanceRegisterRequest request(service_namespace_, service_name_, service_token_, "host", 9092);
  std::string instance_id;
  ReturnCode ret = server_connector->RegisterInstance(request, 10, instance_id);
  ASSERT_EQ(ret, kReturnNetworkFailed);  // 网络错误

  // 服务器错误
  server_connector->SetupExpect(kReturnOk, CreateResponse(v1::StoreLayerException));
  ret = server_connector->RegisterInstance(request, 1000, instance_id);
  ASSERT_EQ(ret, kReturnServerError);

  server_connector->SetupExpect(kReturnOk, CreateResponse(v1::ExecuteSuccess));
  ret = server_connector->RegisterInstance(request, 1000, instance_id);
  ASSERT_EQ(ret, kReturnOk);

  // 重复注册没问题
  server_connector->SetupExpect(kReturnOk, CreateResponse(v1::ExistedResource));
  ret = server_connector->RegisterInstance(request, 1000, instance_id);
  ASSERT_EQ(ret, kReturnExistedResource);
  ASSERT_EQ(instance_id, "instance_id");
}

// 服务反注册
TEST_F(GrpcServerConnectorTest, DeregisterInstance) {
  std::string instance_id = "instance_id";
  InstanceDeregisterRequest deregister_instance(service_token_, instance_id);
  ReturnCode ret = server_connector->DeregisterInstance(deregister_instance, 10);
  ASSERT_EQ(ret, kReturnNetworkFailed);  // 网络错误

  server_connector->SetupExpect(kReturnOk, CreateResponse(v1::ExecuteSuccess));
  ret = server_connector->DeregisterInstance(deregister_instance, 1000);
  ASSERT_EQ(ret, kReturnOk);

  server_connector->SetupExpect(kReturnNetworkFailed, nullptr);
  ret = server_connector->DeregisterInstance(deregister_instance, 1000);
  ASSERT_EQ(ret, kReturnNetworkFailed);

  // 使用HOST+PORT反注册
  server_connector->SetupExpect(kReturnOk, CreateResponse(v1::ExecuteSuccess));
  InstanceDeregisterRequest deregister_host_port(service_namespace_, service_name_, service_token_, "host", 9092);
  deregister_host_port.SetTimeout(10);
  deregister_host_port.SetFlowId(3);
  ret = server_connector->DeregisterInstance(deregister_instance, 1000);
  ASSERT_EQ(ret, kReturnOk);
}

// 服务心跳上报
TEST_F(GrpcServerConnectorTest, InstanceHeartbeat) {
  std::string instance_id = "instance_id";
  InstanceHeartbeatRequest heartbeat_instance(service_token_, instance_id);
  ReturnCode ret = server_connector->InstanceHeartbeat(heartbeat_instance, 10);
  ASSERT_EQ(ret, kReturnNetworkFailed);

  server_connector->SetupExpect(kReturnNetworkFailed, nullptr);
  ret = server_connector->InstanceHeartbeat(heartbeat_instance, 1000);
  ASSERT_EQ(ret, kReturnNetworkFailed);

  // 使用HOST+PORT不断上报心跳
  InstanceHeartbeatRequest heartbeat_host_port(service_namespace_, service_name_, service_token_, "host", 9092);
  int i = 1;
  while (i++ <= 5) {
    heartbeat_host_port.SetFlowId(5 + i);
    server_connector->SetupExpect(kReturnOk, CreateResponse(v1::ExecuteSuccess));
    ret = server_connector->InstanceHeartbeat(heartbeat_host_port, 1000);
    ASSERT_EQ(ret, kReturnOk);
  }
}

TEST_F(GrpcServerConnectorTest, InstanceAsyncHeartbeat) {
  std::string instance_id = "instance_id";
  InstanceHeartbeatRequest heartbeat_instance(service_token_, instance_id);
  // 实际不会执行，检测任务释放没有问题
  ReturnCode ret =
      server_connector->AsyncInstanceHeartbeat(heartbeat_instance, 1000, new TestProviderCallback(kReturnOk, __LINE__));
  ASSERT_EQ(ret, kReturnOk);
  Reactor reactor;

  // 测试连接处理超时
  v1::Instance *instance = new v1::Instance();
  PolarisCallback polaris_callback = [](ReturnCode ret_code, const std::string &message,
                                        std::unique_ptr<v1::Response>) {
    TestProviderCallback callback(kReturnNetworkFailed, __LINE__);
    callback.Response(ret_code, message);
  };
  AsyncRequest *request = new AsyncRequest(reactor, server_connector, kPolarisHeartbeat, Utils::GetNextSeqId(),
                                           instance, 100, polaris_callback);
  ASSERT_TRUE(request->Submit());  // 发起连接
  request->OnConnect(kReturnTimeout);

  // 测试连接建立失败
  instance = new v1::Instance();
  polaris_callback = [](ReturnCode ret_code, const std::string &message, std::unique_ptr<v1::Response>) {
    TestProviderCallback callback(kReturnNetworkFailed, __LINE__);
    callback.Response(ret_code, message);
  };
  request = new AsyncRequest(reactor, server_connector, kPolarisHeartbeat, Utils::GetNextSeqId(), instance, 100,
                             polaris_callback);
  ASSERT_TRUE(request->Submit());  // 发起连接
  request->OnConnect(kReturnNetworkFailed);

  // 测试连接建立成功，但RPC失败
  instance = new v1::Instance();
  polaris_callback = [](ReturnCode ret_code, const std::string &message, std::unique_ptr<v1::Response>) {
    TestProviderCallback callback(kReturnNetworkFailed, __LINE__);
    callback.Response(ret_code, message);
  };
  request = new AsyncRequest(reactor, server_connector, kPolarisHeartbeat, Utils::GetNextSeqId(), instance, 100,
                             polaris_callback);
  ASSERT_TRUE(request->Submit());  // 发起连接
  request->OnConnect(kReturnOk);
  request->OnFailure("grpc rpc failed");

  // 测试连接建立成功，RPC成功
  instance = new v1::Instance();
  polaris_callback = [](ReturnCode ret_code, const std::string &message, std::unique_ptr<v1::Response>) {
    TestProviderCallback callback(kReturnOk, __LINE__);
    callback.Response(ret_code, message);
  };
  request = new AsyncRequest(reactor, server_connector, kPolarisHeartbeat, Utils::GetNextSeqId(), instance, 100,
                             polaris_callback);
  ASSERT_TRUE(request->Submit());  // 发起连接
  request->OnConnect(kReturnOk);
  request->OnSuccess(CreateResponse(v1::ExecuteSuccess));
  reactor.Stop();
}

TEST_F(GrpcServerConnectorTest, AsyncReportClient) {
  ReturnCode retcode = server_connector->AsyncReportClient("", 10, nullptr);
  ASSERT_EQ(retcode, kReturnInvalidArgument);

  retcode = server_connector->AsyncReportClient("2.3.4.5", 0, nullptr);
  ASSERT_EQ(retcode, kReturnInvalidArgument);

  Reactor reactor;
  // 测试连接处理超时
  PolarisCallback polaris_callback = [](ReturnCode ret_code, const std::string &, std::unique_ptr<v1::Response>) {
    ASSERT_EQ(ret_code, kReturnNetworkFailed);
  };
  AsyncRequest *request = new AsyncRequest(reactor, server_connector, kPolarisReportClient, Utils::GetNextSeqId(),
                                           new v1::Client(), 100, polaris_callback);
  ASSERT_TRUE(request->Submit());  // 发起连接
  request->OnConnect(kReturnTimeout);

  // 测试连接建立失败
  polaris_callback = [](ReturnCode ret_code, const std::string &, std::unique_ptr<v1::Response>) {
    ASSERT_EQ(ret_code, kReturnNetworkFailed);
  };
  request = new AsyncRequest(reactor, server_connector, kPolarisReportClient, Utils::GetNextSeqId(), new v1::Client(),
                             100, polaris_callback);
  ASSERT_TRUE(request->Submit());  // 发起连接
  request->OnConnect(kReturnNetworkFailed);

  // 测试连接建立成功，但RPC失败
  polaris_callback = [](ReturnCode ret_code, const std::string &, std::unique_ptr<v1::Response>) {
    ASSERT_EQ(ret_code, kReturnNetworkFailed);
  };
  request = new AsyncRequest(reactor, server_connector, kPolarisReportClient, Utils::GetNextSeqId(), new v1::Client(),
                             100, polaris_callback);
  ASSERT_TRUE(request->Submit());  // 发起连接
  request->OnConnect(kReturnOk);
  request->OnFailure("grpc rpc failed");

  // 测试连接建立成功，RPC成功
  v1::Response *response = new v1::Response();
  response->mutable_code()->set_value(v1::ExecuteSuccess);
  v1::Location *location = response->mutable_client()->mutable_location();
  location->mutable_region()->set_value("华南");
  location->mutable_zone()->set_value("深圳");
  location->mutable_campus()->set_value("深圳-蛇口");
  polaris_callback = [](ReturnCode ret_code, const std::string &, std::unique_ptr<v1::Response> response) {
    ASSERT_EQ(ret_code, kReturnOk);
    ASSERT_TRUE(response != nullptr);
    const v1::Location &location = response->client().location();
    ASSERT_EQ(location.region().value(), "华南");
    ASSERT_EQ(location.zone().value(), "深圳");
    ASSERT_EQ(location.campus().value(), "深圳-蛇口");
  };
  request = new AsyncRequest(reactor, server_connector, kPolarisReportClient, Utils::GetNextSeqId(), new v1::Client(),
                             100, polaris_callback);
  ASSERT_TRUE(request->Submit());  // 发起连接
  request->OnConnect(kReturnOk);
  request->OnSuccess(response);
  reactor.Stop();
}

class MockServiceEventHandler : public ServiceEventHandler {
 public:
  virtual void OnEventUpdate(const ServiceKey & /*service_key*/, ServiceDataType /*data_type*/, void *data) {
    ServiceData *service_data = reinterpret_cast<ServiceData *>(data);
    service_data_queue_.push_back(service_data);
  }

  virtual void OnEventSync(const ServiceKey & /*service_key*/, ServiceDataType /*data_type*/) {}

 public:
  std::vector<ServiceData *> service_data_queue_;
};

TEST_F(GrpcServerConnectorTest, TestDiscoverMutilService) {
  ServiceKey service_key = {service_namespace_, service_name_};
  ServiceKey another_service_key = {"cpp_test_namespace", "another_cpp_test_service"};
  MockServiceEventHandler *handler = new MockServiceEventHandler();
  MockServiceEventHandler *another_handler = new MockServiceEventHandler();
  server_connector->RegisterEventHandler(service_key, kServiceDataInstances, 1000, "", handler);
  server_connector->RegisterEventHandler(another_service_key, kServiceDataInstances, 1000, "", another_handler);
  usleep(1000);
  ASSERT_EQ(handler->service_data_queue_.size(), 0);
  ASSERT_EQ(another_handler->service_data_queue_.size(), 0);
  usleep(1000);
  server_connector->DeregisterEventHandler(service_key, kServiceDataInstances);
  usleep(1000);
}

}  // namespace polaris
