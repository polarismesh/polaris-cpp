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

#include "plugin/server_connector/server_connector.h"

#include <gmock/gmock.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "polaris/accessors.h"
#include "polaris/provider.h"
#include "test_context.h"
#include "test_utils.h"
#include "v1/code.pb.h"

namespace polaris {

class MockGrpcClient : public grpc::GrpcClient {
public:
  explicit MockGrpcClient(Reactor &reactor) : grpc::GrpcClient(reactor) {}
  MOCK_METHOD0(SubmitToReactor, void());
  MOCK_METHOD4(SendRequest, void(google::protobuf::Message &request, const std::string &call_path,
                                 uint64_t timeout, grpc::GrpcRequestCallback &callback));
};

class BlockRequestForTest : public BlockRequest {
public:
  BlockRequestForTest(BlockRequestType request_type, GrpcServerConnector &connector,
                      uint64_t timeout)
      : BlockRequest(request_type, connector, timeout), connector_(connector) {}

  virtual bool PrepareClient() {
    grpc_client_ = mock_grpc_client_;
    instance_    = new Instance("id", "127.0.0.1", 8081, 100);
    return true;
  }

  void SetupExpectCall(grpc::GrpcStatusCode code, v1::Response *response) {
    code_             = code;
    response_         = response;
    mock_grpc_client_ = new MockGrpcClient(connector_.GetReactor());
    EXPECT_CALL(*mock_grpc_client_, SubmitToReactor()).Times(1);
    EXPECT_CALL(*mock_grpc_client_,
                SendRequest(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke(this, &BlockRequestForTest::Callback));
  }

  void Callback(google::protobuf::Message &, const std::string &, uint64_t,
                grpc::GrpcRequestCallback &callback) {
    if (code_ == grpc::kGrpcStatusOk) {
      grpc::Buffer *body = new grpc::Buffer();
      const size_t size  = response_->ByteSizeLong();
      grpc::RawSlice iovec;
      body->Reserve(size, &iovec, 1);
      iovec.len_       = size;
      uint8_t *current = reinterpret_cast<uint8_t *>(iovec.mem_);
      google::protobuf::io::ArrayOutputStream stream(current, size, -1);
      google::protobuf::io::CodedOutputStream codec_stream(&stream);
      response_->SerializeWithCachedSizes(&codec_stream);
      body->Commit(&iovec, 1);
      callback.OnSuccess(body);
      delete response_;
    } else {
      callback.OnFailure(code_, "grpc error");
    }
  }

private:
  MockGrpcClient *mock_grpc_client_;
  GrpcServerConnector &connector_;
  grpc::GrpcStatusCode code_;
  v1::Response *response_;
};

class GrpcServerConnectorForTest : public GrpcServerConnector {
public:
  GrpcServerConnectorForTest() : fake_(false) {
    discover_stream_state_ = kDiscoverStreamGetInstance;
  }

  virtual BlockRequest *CreateBlockRequest(BlockRequestType request_type, uint64_t timeout) {
    sync::MutexGuard guard(lock_);
    if (fake_) {
      BlockRequestForTest *request = new BlockRequestForTest(request_type, *this, timeout);
      request->SetupExpectCall(code_, response_);
      fake_ = false;
      return request;
    } else {
      return GrpcServerConnector::CreateBlockRequest(request_type, timeout);
    }
  }

  virtual ReturnCode SelectInstance(const ServiceKey & /*service_key*/, uint32_t /*timeout*/,
                                    Instance **instance, bool /*ignore_half_open*/) {
    *instance = new Instance("id", "127.0.0.1", 8081, 100);
    return kReturnOk;
  }

  void SetupExpect(grpc::GrpcStatusCode code, v1::Response *response) {
    sync::MutexGuard guard(lock_);
    fake_     = true;
    code_     = code;
    response_ = response;
  }

private:
  sync::Mutex lock_;
  bool fake_;
  grpc::GrpcStatusCode code_;
  v1::Response *response_;
};

class GrpcServerConnectorTest : public ::testing::Test {
protected:
  virtual void SetUp() {
    // default service
    service_namespace_ = "cpp_test_namespace";
    service_name_      = "cpp_test_service";
    service_token_     = "cpp_test_token";
    // create client
    context_ = TestContext::CreateContext();
    std::string err_msg;
    std::string content =
        "addresses: [127.0.0.1:" + StringUtils::TypeToStr(TestUtils::PickUnusedPort()) + "]";
    Config *config = Config::CreateFromString(content, err_msg);
    POLARIS_ASSERT(config != NULL && err_msg.empty());
    server_connector = new GrpcServerConnectorForTest();
    server_connector->Init(config, context_);
    delete config;
  }

  virtual void TearDown() {
    if (server_connector != NULL) {
      delete server_connector;
    }
    if (context_ != NULL) {
      delete context_;
      context_ = NULL;
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

// 服务注册
TEST_F(GrpcServerConnectorTest, RegisterInstance) {
  InstanceRegisterRequest request(service_namespace_, service_name_, service_token_, "host", 9092);
  std::string instance_id;
  ReturnCode ret = server_connector->RegisterInstance(request, 10, instance_id);
  ASSERT_EQ(ret, kReturnNetworkFailed);  // 网络错误

  // 服务器错误
  server_connector->SetupExpect(grpc::kGrpcStatusOk, CreateResponse(v1::StoreLayerException));
  ret = server_connector->RegisterInstance(request, 1000, instance_id);
  ASSERT_EQ(ret, kReturnServerError);

  server_connector->SetupExpect(grpc::kGrpcStatusOk, CreateResponse(v1::ExecuteSuccess));
  ret = server_connector->RegisterInstance(request, 1000, instance_id);
  ASSERT_EQ(ret, kReturnOk);

  // 重复注册没问题
  server_connector->SetupExpect(grpc::kGrpcStatusOk, CreateResponse(v1::ExistedResource));
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

  server_connector->SetupExpect(grpc::kGrpcStatusOk, CreateResponse(v1::ExecuteSuccess));
  ret = server_connector->DeregisterInstance(deregister_instance, 1000);
  ASSERT_EQ(ret, kReturnOk);

  server_connector->SetupExpect(grpc::kGrpcStatusInternal, NULL);
  ret = server_connector->DeregisterInstance(deregister_instance, 1000);
  ASSERT_EQ(ret, kReturnNetworkFailed);

  server_connector->SetupExpect(grpc::kGrpcStatusDeadlineExceeded, NULL);
  ret = server_connector->DeregisterInstance(deregister_instance, 1000);
  ASSERT_EQ(ret, kReturnTimeout);

  // 使用HOST+PORT反注册
  server_connector->SetupExpect(grpc::kGrpcStatusOk, CreateResponse(v1::ExecuteSuccess));
  InstanceDeregisterRequest deregister_host_port(service_namespace_, service_name_, service_token_,
                                                 "host", 9092);
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

  server_connector->SetupExpect(grpc::kGrpcStatusInternal, NULL);
  ret = server_connector->InstanceHeartbeat(heartbeat_instance, 1000);
  ASSERT_EQ(ret, kReturnNetworkFailed);

  // 使用HOST+PORT不断上报心跳
  InstanceHeartbeatRequest heartbeat_host_port(service_namespace_, service_name_, service_token_,
                                               "host", 9092);
  int i = 1;
  while (i++ <= 5) {
    heartbeat_host_port.SetFlowId(5 + i);
    server_connector->SetupExpect(grpc::kGrpcStatusOk, CreateResponse(v1::ExecuteSuccess));
    ret = server_connector->InstanceHeartbeat(heartbeat_host_port, 1000);
    ASSERT_EQ(ret, kReturnOk);
  }
}

TEST_F(GrpcServerConnectorTest, InstanceAsyncHeartbeat) {
  std::string instance_id = "instance_id";
  InstanceHeartbeatRequest heartbeat_instance(service_token_, instance_id);
  // 实际不会执行，检测任务释放没有问题
  ReturnCode ret = server_connector->AsyncInstanceHeartbeat(
      heartbeat_instance, 1000, new TestProviderCallback(kReturnOk, __LINE__));
  ASSERT_EQ(ret, kReturnOk);
  Reactor reactor;

  // 测试连接处理超时
  v1::Instance *instance         = new v1::Instance();
  TestProviderCallback *callback = new TestProviderCallback(kReturnNetworkFailed, __LINE__);
  AsyncRequest *request =
      new AsyncRequest(reactor, server_connector, Utils::GetNextSeqId(), instance, 100, callback);
  ASSERT_TRUE(request->Submit());  // 发起连接
  request->OnConnectTimeout();

  // 测试连接建立失败
  instance = new v1::Instance();
  callback = new TestProviderCallback(kReturnNetworkFailed, __LINE__);
  request =
      new AsyncRequest(reactor, server_connector, Utils::GetNextSeqId(), instance, 100, callback);
  ASSERT_TRUE(request->Submit());  // 发起连接
  request->OnConnectFailed();

  // 测试连接建立成功，但RPC失败
  instance = new v1::Instance();
  callback = new TestProviderCallback(kReturnNetworkFailed, __LINE__);
  request =
      new AsyncRequest(reactor, server_connector, Utils::GetNextSeqId(), instance, 100, callback);
  ASSERT_TRUE(request->Submit());  // 发起连接
  request->OnConnectSuccess();
  request->OnFailure(grpc::kGrpcStatusInternal, "grpc rpc failed");

  // 测试连接建立成功，但RPC成功
  instance = new v1::Instance();
  callback = new TestProviderCallback(kReturnOk, __LINE__);
  request =
      new AsyncRequest(reactor, server_connector, Utils::GetNextSeqId(), instance, 100, callback);
  ASSERT_TRUE(request->Submit());  // 发起连接
  request->OnConnectSuccess();
  request->OnSuccess(CreateResponse(v1::ExecuteSuccess));
}

TEST_F(GrpcServerConnectorTest, ReportClient) {
  Location client_location;
  ReturnCode retcode = server_connector->ReportClient("", 10, client_location);
  ASSERT_EQ(retcode, kReturnInvalidArgument);

  retcode = server_connector->ReportClient("2.3.4.5", 10, client_location);
  ASSERT_EQ(retcode, kReturnNetworkFailed);

  server_connector->SetupExpect(grpc::kGrpcStatusDeadlineExceeded, NULL);
  retcode = server_connector->ReportClient("2.3.4.5", 1000, client_location);
  ASSERT_EQ(retcode, kReturnTimeout);

  v1::Response *response = new v1::Response();
  response->mutable_code()->set_value(v1::CMDBNotFindHost);
  server_connector->SetupExpect(grpc::kGrpcStatusOk, response);
  retcode = server_connector->ReportClient("2.3.4.5", 1000, client_location);
  ASSERT_EQ(retcode, kReturnResourceNotFound);

  response = new v1::Response();
  response->mutable_code()->set_value(v1::ExecuteSuccess);
  v1::Location *location = response->mutable_client()->mutable_location();
  location->mutable_region()->set_value("华南");
  location->mutable_zone()->set_value("深圳");
  location->mutable_campus()->set_value("深圳-蛇口");
  server_connector->SetupExpect(grpc::kGrpcStatusOk, response);
  retcode = server_connector->ReportClient("2.3.4.5", 1000, client_location);
  ASSERT_EQ(retcode, kReturnOk);
  ASSERT_EQ(client_location.region, "华南");
  ASSERT_EQ(client_location.zone, "深圳");
  ASSERT_EQ(client_location.campus, "深圳-蛇口");
}

class MockServiceEventHandler : public ServiceEventHandler {
public:
  virtual void OnEventUpdate(const ServiceKey & /*service_key*/, ServiceDataType /*data_type*/,
                             void *data) {
    ServiceData *service_data = reinterpret_cast<ServiceData *>(data);
    service_data_queue_.push_back(service_data);
  }

  virtual void OnEventSync(const ServiceKey & /*service_key*/, ServiceDataType /*data_type*/) {}

public:
  std::vector<ServiceData *> service_data_queue_;
};

TEST_F(GrpcServerConnectorTest, TestDiscoverMutilService) {
  ServiceKey service_key                   = {service_namespace_, service_name_};
  ServiceKey another_service_key           = {"cpp_test_namespace", "another_cpp_test_service"};
  MockServiceEventHandler *handler         = new MockServiceEventHandler();
  MockServiceEventHandler *another_handler = new MockServiceEventHandler();
  server_connector->RegisterEventHandler(service_key, kServiceDataInstances, 1000, handler);
  server_connector->RegisterEventHandler(another_service_key, kServiceDataInstances, 1000,
                                         another_handler);
  usleep(1000);
  ASSERT_EQ(handler->service_data_queue_.size(), 0);
  ASSERT_EQ(another_handler->service_data_queue_.size(), 0);
  usleep(1000);
  server_connector->DeregisterEventHandler(service_key, kServiceDataInstances);
  usleep(1000);
}

}  // namespace polaris
