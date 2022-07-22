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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <pthread.h>
#include <stdint.h>

#include "context/context_impl.h"
#include "mock/fake_server_response.h"
#include "mock/mock_dynamic_weight_connector.h"
#include "mock/mock_server_connector.h"
#include "polaris/plugin.h"
#include "polaris/provider.h"
#include "test_utils.h"
#include "utils/file_utils.h"

namespace polaris {

// 创建API测试
class ProviderApiCreateTest : public ::testing::Test {
 protected:
  virtual void SetUp() { config_ = nullptr; }

  virtual void TearDown() { DeleteConfig(); }

  void CreateConfig() {
    std::string err_msg;
    content_ =
        "global:\n"
        "  serverConnector:\n"
        "    addresses:\n"
        "      - 127.0.0.1:8081";
    config_ = Config::CreateFromString(content_, err_msg);
    ASSERT_TRUE(config_ != nullptr && err_msg.empty()) << err_msg;
  }

  void DeleteConfig() {
    if (config_ != nullptr) {
      delete config_;
      config_ = nullptr;
    }
  }

 protected:
  std::string content_;
  Config *config_;
};

TEST_F(ProviderApiCreateTest, TestCreateFromContext) {
  ProviderApi *provider_api = ProviderApi::Create(nullptr);
  ASSERT_FALSE(provider_api != nullptr);  // Context为NULL无法创建

  this->CreateConfig();
  Context *context = Context::Create(config_);  // share context
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  provider_api = ProviderApi::Create(context);
  ASSERT_TRUE(provider_api != nullptr);
  delete provider_api;
  EXPECT_NO_THROW(delete context);

  this->CreateConfig();
  context = Context::Create(config_, kNotInitContext);
  this->DeleteConfig();
  ASSERT_TRUE(context == nullptr);
  provider_api = ProviderApi::Create(context);
  ASSERT_FALSE(provider_api != nullptr);

  this->CreateConfig();
  context = Context::Create(config_, kShareContext);  // share context
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  provider_api = ProviderApi::Create(context);
  ASSERT_TRUE(provider_api != nullptr);
  delete provider_api;
  EXPECT_NO_THROW(delete context);

  this->CreateConfig();
  context = Context::Create(config_, kShareContextWithoutEngine);
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  provider_api = ProviderApi::Create(context);
  ASSERT_FALSE(provider_api != nullptr);  // mode不对无法创建
  delete context;

  this->CreateConfig();
  context = Context::Create(config_, kPrivateContext);
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  provider_api = ProviderApi::Create(context);
  ASSERT_TRUE(provider_api != nullptr);
  delete provider_api;
}

TEST_F(ProviderApiCreateTest, TestCreateFromConfig) {
  Config *config = nullptr;
  ProviderApi *provider_api = ProviderApi::CreateFromConfig(config);
  ASSERT_FALSE(provider_api != nullptr);  // 配置为null， 无法创建provider api

  std::string err_msg, content =
                           "global:\n"
                           "  serverConnector:\n"
                           "    addresses: []";
  config = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(config != nullptr && err_msg.empty());
  provider_api = ProviderApi::CreateFromConfig(config);
  ASSERT_FALSE(provider_api != nullptr);  // 缺少server地址配置无法创建context，导致无法创建provider api
  delete config;

  this->CreateConfig();
  provider_api = ProviderApi::CreateFromConfig(config_);
  this->DeleteConfig();
  ASSERT_TRUE(provider_api != nullptr);  // 正常创建
  delete provider_api;

  provider_api = ProviderApi::CreateWithDefaultFile();
  ASSERT_TRUE(provider_api != nullptr);  // 正常创建
  delete provider_api;
}

TEST_F(ProviderApiCreateTest, TestCreateFromFile) {
  ProviderApi *provider_api = ProviderApi::CreateFromFile("not_exist.file");
  ASSERT_FALSE(provider_api != nullptr);  // 从不存在的文件创建失败

  // 创建临时文件
  std::string config_file;
  TestUtils::CreateTempFile(config_file);

  provider_api = ProviderApi::CreateFromFile(config_file);
  ASSERT_TRUE(provider_api != nullptr);  // 从空文件可以初始化Context从而创建Provider Api
  delete provider_api;
  FileUtils::RemoveFile(config_file);

  // 写入配置
  TestUtils::CreateTempFileWithContent(config_file, content_);
  provider_api = ProviderApi::CreateFromFile(config_file);
  ASSERT_TRUE(provider_api != nullptr);  // 创建成功
  delete provider_api;
  FileUtils::RemoveFile(config_file);
}

TEST_F(ProviderApiCreateTest, TestCreateFromString) {
  std::string content;
  ProviderApi *provider_api = ProviderApi::CreateFromString(content);
  ASSERT_TRUE(provider_api != nullptr);  // 空字符串可以创建
  delete provider_api;

  content = "[,,,";
  provider_api = ProviderApi::CreateFromString(content);
  ASSERT_FALSE(provider_api != nullptr);  // 错误的字符串无法创建

  provider_api = ProviderApi::CreateFromString(content_);
  ASSERT_TRUE(provider_api != nullptr);  // 创建成功
  delete provider_api;
}

// 通过 MockServerConnector测试Provider Api
class ProviderApiMockServerConnectorTest : public MockServerConnectorTest {
 protected:
  virtual void SetUp() {
    // inject and mock dynamic weight connector
    ContextImpl::SetDynamicWeightConnectorCreator(MockDynamicWeightCreator);
    // mock server connector
    MockServerConnectorTest::SetUp();
    TestUtils::CreateTempDir(persist_dir_);
    std::string err_msg, content =
                             "global:\n"
                             "  serverConnector:\n"
                             "    protocol: " +
                             server_connector_plugin_name_ +
                             "\nconsumer:\n"
                             "  localCache:\n"
                             "    persistDir: " +
                             persist_dir_;
    Config *config = Config::CreateFromString(content, err_msg);
    POLARIS_ASSERT(config != nullptr && err_msg.empty());
    context_ = Context::Create(config);
    ASSERT_TRUE(context_ != nullptr);
    delete config;
    provider_api_ = ProviderApi::Create(context_);
    ASSERT_TRUE(provider_api_ != nullptr);

    // check
    MockServerConnector *server_connector_in_context =
        dynamic_cast<MockServerConnector *>(context_->GetContextImpl()->GetServerConnector());
    ASSERT_TRUE(server_connector_ != nullptr);
    ASSERT_EQ(server_connector_, server_connector_in_context);
  }

  virtual void TearDown() {
    delete provider_api_;
    delete context_;
    TestUtils::RemoveDir(persist_dir_);
    for (std::size_t i = 0; i < event_thread_list_.size(); ++i) {
      pthread_join(event_thread_list_[i], nullptr);
    }
    MockServerConnectorTest::TearDown();
  }

 public:
  void HeartbeatServerHandler(const ServiceKey &service_key, ServiceDataType data_type, uint64_t /*sync_interval*/,
                              const std::string & /*disk_revision*/, ServiceEventHandler *handler) {
    ServiceData *service_data;
    v1::DiscoverResponse response;
    SeedServerConfig seed_config;
    ServiceKey &heartbeat_service = seed_config.heartbeat_cluster_.service_;
    const std::string &protocol = "mock";
    if (data_type == kServiceDataInstances) {
      FakeServer::InstancesResponse(response, heartbeat_service);
      for (int i = 0; i < 6; i++) {
        ::v1::Instance *instance = response.mutable_instances()->Add();
        instance->mutable_id()->set_value("instance_" + std::to_string(i));
        instance->mutable_host()->set_value("host" + std::to_string(i));
        instance->mutable_port()->set_value(i);
        instance->mutable_weight()->set_value(100);
        if (i % 3 == 0) {
          (*instance->mutable_metadata())["protocol"] = protocol;
        }
      }
      service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
    } else {
      FakeServer::RoutingResponse(response, heartbeat_service);
      v1::Routing *routing = response.mutable_routing();
      v1::Route *route = routing->add_inbounds();
      v1::Source *source = route->add_sources();
      source->mutable_namespace_()->set_value("*");
      source->mutable_service()->set_value("*");
      v1::MatchString exact_string;
      exact_string.mutable_value()->set_value(protocol);
      (*source->mutable_metadata())["protocol"] = exact_string;
      v1::Destination *destination = route->add_destinations();
      destination->mutable_namespace_()->set_value(heartbeat_service.namespace_);
      destination->mutable_service()->set_value(heartbeat_service.name_);
      (*destination->mutable_metadata())["protocol"] = exact_string;
      service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
    }
    // 创建单独的线程去下发数据更新，否则会死锁
    EventHandlerData *event_data = new EventHandlerData();
    event_data->service_key_ = service_key;
    event_data->data_type_ = data_type;
    event_data->service_data_ = service_data;
    event_data->handler_ = handler;
    pthread_t tid;
    pthread_create(&tid, nullptr, AsyncEventUpdate, event_data);
    handler_list_.push_back(handler);
    event_thread_list_.push_back(tid);
  }

 protected:
  Context *context_;
  std::string persist_dir_;
  ProviderApi *provider_api_;
  std::vector<pthread_t> event_thread_list_;
};

TEST_F(ProviderApiMockServerConnectorTest, TestInstanceRegisterArgumentCheck) {
  std::string service_namespace = "service_namespace";
  std::string service_name = "service_name";
  std::string service_token = "service_token";
  std::string instance_host = "instance_host";
  std::string instance_id;
  int port = 42;
  std::string empty_string;

  InstanceRegisterRequest namespace_empty_request(empty_string, service_name, service_token, instance_host, port);
  EXPECT_EQ(provider_api_->Register(namespace_empty_request, instance_id), kReturnInvalidArgument);
  EXPECT_TRUE(instance_id.empty());

  InstanceRegisterRequest name_empty_request(service_namespace, empty_string, service_token, instance_host, port);
  EXPECT_EQ(provider_api_->Register(name_empty_request, instance_id), kReturnInvalidArgument);
  EXPECT_TRUE(instance_id.empty());

  InstanceRegisterRequest token_empty_request(service_namespace, service_name, empty_string, instance_host, port);
  EXPECT_EQ(provider_api_->Register(token_empty_request, instance_id), kReturnInvalidArgument);
  EXPECT_TRUE(instance_id.empty());

  InstanceRegisterRequest host_empty_request(service_namespace, service_name, service_token, empty_string, port);
  EXPECT_EQ(provider_api_->Register(host_empty_request, instance_id), kReturnInvalidArgument);
  EXPECT_TRUE(instance_id.empty());

  InstanceRegisterRequest zero_port_request(service_namespace, service_name, service_token, instance_host, 0);
  EXPECT_EQ(provider_api_->Register(zero_port_request, instance_id), kReturnInvalidArgument);
  EXPECT_TRUE(instance_id.empty());

  InstanceRegisterRequest negative_port_request(service_namespace, service_name, service_token, instance_host, -1);
  EXPECT_EQ(provider_api_->Register(negative_port_request, instance_id), kReturnInvalidArgument);
  EXPECT_TRUE(instance_id.empty());

  InstanceRegisterRequest big_port_request(service_namespace, service_name, service_token, instance_host, 65535 + 1);
  EXPECT_EQ(provider_api_->Register(big_port_request, instance_id), kReturnInvalidArgument);
  EXPECT_TRUE(instance_id.empty());

  std::string return_instance = "return_instance";
  EXPECT_CALL(*server_connector_, RegisterInstance(::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SetArgReferee<2>(return_instance), ::testing::Return(kReturnOk)));
  InstanceRegisterRequest normal_request(service_namespace, service_name, service_token, instance_host, port);
  EXPECT_EQ(provider_api_->Register(normal_request, instance_id), kReturnOk);
  EXPECT_EQ(instance_id, return_instance);

  // testing heartbeat
  return_instance = "instance_id_heartbeat";
  EXPECT_CALL(*server_connector_, RegisterInstance(::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SetArgReferee<2>(return_instance), ::testing::Return(kReturnOk)));
  InstanceRegisterRequest heartbeat_register_request(service_namespace, service_name, service_token, instance_host,
                                                     port);
  heartbeat_register_request.SetHealthCheckFlag(false);
  EXPECT_EQ(provider_api_->Register(heartbeat_register_request, instance_id), kReturnOk);
  EXPECT_EQ(instance_id, return_instance);

  // testing heartbeat
  return_instance = "instance_id_heartbeat";
  EXPECT_CALL(*server_connector_, RegisterInstance(::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SetArgReferee<2>(return_instance), ::testing::Return(kReturnOk)));

  EXPECT_CALL(*server_connector_, InstanceHeartbeat(::testing::_, ::testing::_))
      .WillRepeatedly(::testing::Return(kReturnOk));
  heartbeat_register_request.SetHealthCheckFlag(true);
  heartbeat_register_request.SetTtl(2);  // 心跳间隔2s
  EXPECT_EQ(provider_api_->Register(heartbeat_register_request, instance_id), kReturnOk);
  EXPECT_EQ(instance_id, return_instance);
  sleep(6);
}

TEST_F(ProviderApiMockServerConnectorTest, TestInstanceDeregisterArgumentCheck) {
  std::string service_namespace = "service_namespace";
  std::string service_name = "service_name";
  std::string service_token = "service_token";
  std::string instance_host = "instance_host";
  std::string instance_id = "instance_id";
  int port = 42;
  std::string empty_string;

  InstanceDeregisterRequest namespace_empty_request(empty_string, service_name, service_token, instance_host, port);
  EXPECT_EQ(provider_api_->Deregister(namespace_empty_request), kReturnInvalidArgument);

  InstanceDeregisterRequest name_empty_request(service_namespace, empty_string, service_token, instance_host, port);
  EXPECT_EQ(provider_api_->Deregister(name_empty_request), kReturnInvalidArgument);

  InstanceDeregisterRequest token_empty_request(service_namespace, service_name, empty_string, instance_host, port);
  EXPECT_EQ(provider_api_->Deregister(token_empty_request), kReturnInvalidArgument);

  InstanceDeregisterRequest host_empty_request(service_namespace, service_name, service_token, empty_string, port);
  EXPECT_EQ(provider_api_->Deregister(host_empty_request), kReturnInvalidArgument);

  InstanceDeregisterRequest zero_port_request(service_namespace, service_name, service_token, instance_host, 0);
  EXPECT_EQ(provider_api_->Deregister(zero_port_request), kReturnInvalidArgument);

  InstanceDeregisterRequest negative_port_request(service_namespace, service_name, service_token, instance_host, -1);
  EXPECT_EQ(provider_api_->Deregister(negative_port_request), kReturnInvalidArgument);

  InstanceDeregisterRequest big_port_request(service_namespace, service_name, service_token, instance_host, 65535 + 1);
  EXPECT_EQ(provider_api_->Deregister(big_port_request), kReturnInvalidArgument);

  InstanceDeregisterRequest instance_id_empty_request(service_token, empty_string);
  EXPECT_EQ(provider_api_->Deregister(instance_id_empty_request), kReturnInvalidArgument);

  InstanceDeregisterRequest instance_token_empty_request(empty_string, instance_id);
  EXPECT_EQ(provider_api_->Deregister(instance_token_empty_request), kReturnInvalidArgument);

  EXPECT_CALL(*server_connector_, DeregisterInstance(::testing::_, ::testing::_))
      .Times(2)
      .WillRepeatedly(::testing::Return(kReturnOk));
  InstanceDeregisterRequest normal_host_port_request(service_namespace, service_name, service_token, instance_host,
                                                     port);
  EXPECT_EQ(provider_api_->Deregister(normal_host_port_request), kReturnOk);
  InstanceDeregisterRequest normal_instance_id_request(service_token, instance_id);
  EXPECT_EQ(provider_api_->Deregister(normal_instance_id_request), kReturnOk);
}

TEST_F(ProviderApiMockServerConnectorTest, TestInstanceHeartbeatArgumentCheck) {
  std::string service_namespace = "service_namespace";
  std::string service_name = "service_name";
  std::string service_token = "service_token";
  std::string instance_host = "instance_host";
  std::string instance_id = "instance_id";
  int port = 42;
  std::string empty_string;

  InstanceHeartbeatRequest namespace_empty_request(empty_string, service_name, service_token, instance_host, port);
  EXPECT_EQ(provider_api_->Heartbeat(namespace_empty_request), kReturnInvalidArgument);

  InstanceHeartbeatRequest name_empty_request(service_namespace, empty_string, service_token, instance_host, port);
  EXPECT_EQ(provider_api_->Heartbeat(name_empty_request), kReturnInvalidArgument);

  InstanceHeartbeatRequest token_empty_request(service_namespace, service_name, empty_string, instance_host, port);
  EXPECT_EQ(provider_api_->Heartbeat(token_empty_request), kReturnInvalidArgument);

  InstanceHeartbeatRequest host_empty_request(service_namespace, service_name, service_token, empty_string, port);
  EXPECT_EQ(provider_api_->Heartbeat(host_empty_request), kReturnInvalidArgument);

  InstanceHeartbeatRequest zero_port_request(service_namespace, service_name, service_token, instance_host, 0);
  EXPECT_EQ(provider_api_->Heartbeat(zero_port_request), kReturnInvalidArgument);

  InstanceHeartbeatRequest negative_port_request(service_namespace, service_name, service_token, instance_host, -1);
  EXPECT_EQ(provider_api_->Heartbeat(negative_port_request), kReturnInvalidArgument);

  InstanceHeartbeatRequest big_port_request(service_namespace, service_name, service_token, instance_host, 65535 + 1);
  EXPECT_EQ(provider_api_->Heartbeat(big_port_request), kReturnInvalidArgument);

  InstanceHeartbeatRequest instance_id_empty_request(service_token, empty_string);
  EXPECT_EQ(provider_api_->Heartbeat(instance_id_empty_request), kReturnInvalidArgument);

  InstanceHeartbeatRequest instance_token_empty_request(empty_string, instance_id);
  EXPECT_EQ(provider_api_->Heartbeat(instance_token_empty_request), kReturnInvalidArgument);
}

TEST_F(ProviderApiMockServerConnectorTest, TestInstanceHeartbeat) {
  EXPECT_CALL(*server_connector_, InstanceHeartbeat(::testing::_, ::testing::_))
      .Times(2)
      .WillRepeatedly(::testing::Return(kReturnOk));

  InstanceHeartbeatRequest normal_host_port_request("service_namespace", "service_name", "service_token",
                                                    "instance_host", 8000);
  EXPECT_EQ(provider_api_->Heartbeat(normal_host_port_request), kReturnOk);
  InstanceHeartbeatRequest normal_instance_id_request("service_token", "instance_id");
  EXPECT_EQ(provider_api_->Heartbeat(normal_instance_id_request), kReturnOk);
}

TEST_F(ProviderApiMockServerConnectorTest, TestInstanceAsyncHeartbeatFailed) {
  EXPECT_CALL(*server_connector_, AsyncInstanceHeartbeat(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillRepeatedly(::testing::Return(kReturnInvalidArgument));

  InstanceHeartbeatRequest normal_host_port_request("service_namespace", "service_name", "service_token",
                                                    "instance_host", 8000);
  TestProviderCallback *callback = new TestProviderCallback(kReturnInvalidArgument, __LINE__);
  EXPECT_EQ(provider_api_->AsyncHeartbeat(normal_host_port_request, callback), kReturnInvalidArgument);
}

// test dynamic weight connector
TEST_F(ProviderApiMockServerConnectorTest, TestReportDynamicWeight) {
  // mock function InstanceReportDynamicWeight
  DynamicWeightConnector *connector = context_->GetContextImpl()->GetDynamicWeightConnector();
  MockDynamicWeightConnector &mock_conector = *dynamic_cast<MockDynamicWeightConnector *>(connector);
  EXPECT_CALL(mock_conector, InstanceReportDynamicWeight(::testing::_, ::testing::_))
      .Times(1)
      .WillRepeatedly(::testing::Return(kReturnOk));

  // call function test
  DynamicWeightRequest dynamicweight_req("service_namespace", "service_name", "service_token", "instance_host", 8000);
  EXPECT_EQ(provider_api_->ReportDynamicWeight(dynamicweight_req), kReturnOk);
}

}  // namespace polaris
