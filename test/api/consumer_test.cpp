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

#include "api/consumer_api.h"
#include "context/context_impl.h"
#include "mock/fake_server_response.h"
#include "mock/mock_server_connector.h"
#include "plugin/load_balancer/hash/hash_manager.h"
#include "polaris/consumer.h"
#include "polaris/plugin.h"
#include "test_utils.h"
#include "utils/file_utils.h"

namespace polaris {

// 创建API测试
class ConsumerApiCreateTest : public ::testing::Test {
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

TEST_F(ConsumerApiCreateTest, TestCreateFromContext) {
  ConsumerApi *consumer_api = ConsumerApi::Create(nullptr);
  ASSERT_FALSE(consumer_api != nullptr);  // Context为NULL无法创建

  this->CreateConfig();
  Context *context = Context::Create(config_);  // share context
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  consumer_api = ConsumerApi::Create(context);
  ASSERT_TRUE(consumer_api != nullptr);
  delete consumer_api;
  EXPECT_NO_THROW(delete context);

  this->CreateConfig();
  context = Context::Create(config_, kNotInitContext);
  this->DeleteConfig();
  ASSERT_TRUE(context == nullptr);
  consumer_api = ConsumerApi::Create(context);
  ASSERT_FALSE(consumer_api != nullptr);

  this->CreateConfig();
  context = Context::Create(config_, kShareContext);  // share context
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  consumer_api = ConsumerApi::Create(context);
  ASSERT_TRUE(consumer_api != nullptr);
  delete consumer_api;
  EXPECT_NO_THROW(delete context);

  this->CreateConfig();
  context = Context::Create(config_, kShareContextWithoutEngine);
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  consumer_api = ConsumerApi::Create(context);
  ASSERT_FALSE(consumer_api != nullptr);  // mode不对无法创建
  delete context;

  this->CreateConfig();
  context = Context::Create(config_, kPrivateContext);
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  consumer_api = ConsumerApi::Create(context);
  ASSERT_TRUE(consumer_api != nullptr);
  delete consumer_api;
}

TEST_F(ConsumerApiCreateTest, TestCreateFromConfig) {
  Config *config = nullptr;
  ConsumerApi *consumer_api = ConsumerApi::CreateFromConfig(config);
  ASSERT_FALSE(consumer_api != nullptr);  // 配置为null， 无法创建consumer api

  std::string err_msg, content =
                           "global:\n"
                           "  serverConnector:\n"
                           "    addresses: []";
  config = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(config != nullptr && err_msg.empty());
  consumer_api = ConsumerApi::CreateFromConfig(config);
  ASSERT_FALSE(consumer_api != nullptr);  // 缺少server地址配置无法创建context，导致无法创建consumer api
  delete config;

  this->CreateConfig();
  consumer_api = ConsumerApi::CreateFromConfig(config_);
  this->DeleteConfig();
  ASSERT_TRUE(consumer_api != nullptr);  // 正常创建
  delete consumer_api;

  consumer_api = ConsumerApi::CreateWithDefaultFile();
  ASSERT_TRUE(consumer_api != nullptr);  // 正常创建
  delete consumer_api;
}

TEST_F(ConsumerApiCreateTest, TestCreateFromFile) {
  ConsumerApi *consumer_api = ConsumerApi::CreateFromFile("not_exist.file");
  ASSERT_FALSE(consumer_api != nullptr);  // 从不存在的文件创建失败

  // 创建临时文件
  std::string config_file;
  TestUtils::CreateTempFile(config_file);

  consumer_api = ConsumerApi::CreateFromFile(config_file);
  ASSERT_TRUE(consumer_api != nullptr);  // 从空文件可以初始化Context从而创建consumer Api
  delete consumer_api;
  FileUtils::RemoveFile(config_file);

  // 写入配置
  TestUtils::CreateTempFileWithContent(config_file, content_);
  consumer_api = ConsumerApi::CreateFromFile(config_file);
  ASSERT_TRUE(consumer_api != nullptr);  // 创建成功
  delete consumer_api;
  FileUtils::RemoveFile(config_file);
}

TEST_F(ConsumerApiCreateTest, TestCreateFromString) {
  std::string content;
  ConsumerApi *consumer_api = ConsumerApi::CreateFromString(content);
  ASSERT_TRUE(consumer_api != nullptr);  // 空字符串可创建
  delete consumer_api;

  content = "[,,,";
  consumer_api = ConsumerApi::CreateFromString(content);
  ASSERT_FALSE(consumer_api != nullptr);  // 错误的字符串无法创建

  consumer_api = ConsumerApi::CreateFromString(content_);
  ASSERT_TRUE(consumer_api != nullptr);  // 创建成功
  delete consumer_api;
}

class ConsumerApiMockServerConnectorTest : public MockServerConnectorTest {
 protected:
  virtual void SetUp() {
    // mock server connector
    MockServerConnectorTest::SetUp();
    context_ = nullptr;
    consumer_api_ = nullptr;
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
    delete config;
    ASSERT_TRUE(context_ != nullptr);
    ASSERT_TRUE((consumer_api_ = ConsumerApi::Create(context_)) != nullptr);

    // check
    MockServerConnector *server_connector_in_context =
        dynamic_cast<MockServerConnector *>(context_->GetContextImpl()->GetServerConnector());
    ASSERT_TRUE(server_connector_ != nullptr);
    ASSERT_EQ(server_connector_, server_connector_in_context);
    service_key_.name_ = "cpp_test_service";
    service_key_.namespace_ = "cpp_test_namespace";
    instance_num_ = 10;
    instance_healthy_ = true;

    v1::CircuitBreaker *cb = circuit_breaker_pb_response_.mutable_circuitbreaker();
    cb->mutable_name()->set_value("xxx");
    cb->mutable_namespace_()->set_value("xxx");
  }

  virtual void TearDown() {
    if (consumer_api_ != nullptr) {
      delete consumer_api_;
      consumer_api_ = nullptr;
    }
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
    TestUtils::RemoveDir(persist_dir_);
    for (std::size_t i = 0; i < event_thread_list_.size(); ++i) {
      pthread_join(event_thread_list_[i], nullptr);
    }
    MockServerConnectorTest::TearDown();
  }

  void InitServiceData() {
    FakeServer::InstancesResponse(instances_response_, service_key_);
    v1::Service *service = instances_response_.mutable_service();
    for (int i = 0; i < 10; i++) {
      (*service->mutable_metadata())["key" + std::to_string(i)] = "value" + std::to_string(i);
    }
    for (int i = 0; i < instance_num_ + 2; i++) {
      ::v1::Instance *instance = instances_response_.mutable_instances()->Add();
      instance->mutable_namespace_()->set_value(service_key_.namespace_);
      instance->mutable_service()->set_value(service_key_.name_);
      instance->mutable_id()->set_value("instance_" + std::to_string(i));
      instance->mutable_host()->set_value("host" + std::to_string(i));
      instance->mutable_port()->set_value(8080 + i);
      instance->mutable_healthy()->set_value(instance_healthy_);
      instance->mutable_weight()->set_value(i != instance_num_ ? 100 : 0);  // 第11个权重为0
      if (i == instance_num_ + 1) {
        instance->mutable_isolate()->set_value(true);  // 第12个隔离
      }
    }
    FakeServer::RoutingResponse(routing_response_, service_key_);
  }

 public:
  void MockFireEventHandler(const ServiceKey &service_key, ServiceDataType data_type, uint64_t /*sync_interval*/,
                            const std::string & /*disk_revision*/, ServiceEventHandler *handler) {
    ServiceData *service_data;
    if (data_type == kServiceDataInstances) {
      service_data = ServiceData::CreateFromPb(&instances_response_, kDataIsSyncing);
    } else if (data_type == kServiceDataRouteRule) {
      service_data = ServiceData::CreateFromPb(&routing_response_, kDataIsSyncing);
    } else if (data_type == kCircuitBreakerConfig) {
      service_data = ServiceData::CreateFromPb(&circuit_breaker_pb_response_, kDataIsSyncing);
    } else {
      service_data = ServiceData::CreateFromPb(&routing_response_, kDataIsSyncing);
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
  ConsumerApi *consumer_api_;
  v1::DiscoverResponse instances_response_;
  v1::DiscoverResponse routing_response_;
  v1::DiscoverResponse circuit_breaker_pb_response_;
  ServiceKey service_key_;
  int instance_num_;
  bool instance_healthy_;
  std::string persist_dir_;
  std::vector<pthread_t> event_thread_list_;
};

TEST_F(ConsumerApiMockServerConnectorTest, TestGetOneInstanceRequest) {
  ServiceKey service_key;
  GetOneInstanceRequest empty_service_name_request(service_key);
  Instance instance;
  ASSERT_EQ(consumer_api_->GetOneInstance(empty_service_name_request, instance), kReturnInvalidArgument);
  ASSERT_TRUE(instance.GetId().empty());
  InstancesResponse *response = nullptr;
  ASSERT_EQ(consumer_api_->GetOneInstance(empty_service_name_request, response), kReturnInvalidArgument);
  ASSERT_FALSE(response != nullptr);

  GetOneInstanceRequest request(service_key_);
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockFireEventHandler),
                           ::testing::Return(kReturnOk)));

  ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
  ASSERT_TRUE(!instance.GetId().empty());
  ASSERT_EQ(consumer_api_->GetOneInstance(request, response), kReturnOk);
  ASSERT_TRUE(response != nullptr);
  delete response;
}

TEST_F(ConsumerApiMockServerConnectorTest, TestGetOneInstanceTimeout) {
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockIgnoreEventHandler),
                           ::testing::Return(kReturnOk)));

  GetOneInstanceRequest request(service_key_);
  Instance instance;
  ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnTimeout);
  ASSERT_TRUE(instance.GetId().empty());

  InstancesResponse *response = nullptr;
  ASSERT_EQ(consumer_api_->GetOneInstance(request, response), kReturnTimeout);
  ASSERT_FALSE(response != nullptr);
}

TEST_F(ConsumerApiMockServerConnectorTest, TestGetOneInstanceButNoHealthyInstances) {
  instance_healthy_ = false;
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockFireEventHandler),
                           ::testing::Return(kReturnOk)));

  GetOneInstanceRequest request(service_key_);
  Instance instance;
  // 全部实例都不健康，由于路由模块默认最少返回比例大于0，依然会返回实例
  ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
  ASSERT_TRUE(!instance.GetId().empty());

  InstancesResponse *response = nullptr;
  ASSERT_EQ(consumer_api_->GetOneInstance(request, response), kReturnOk);
  ASSERT_TRUE(response != nullptr);
  delete response;
}

TEST_F(ConsumerApiMockServerConnectorTest, TestGetOneInstanceWithOnlyOneInstance) {
  instance_num_ = 1;
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockFireEventHandler),
                           ::testing::Return(kReturnOk)));

  Instance instance;
  InstancesResponse *response = nullptr;
  for (int i = 0; i < 100; i++) {
    GetOneInstanceRequest request(service_key_);
    ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    ASSERT_EQ(instance.GetId(), "instance_0");

    ASSERT_EQ(consumer_api_->GetOneInstance(request, response), kReturnOk);
    ASSERT_TRUE(response != nullptr);
    ASSERT_EQ(response->GetInstances().size(), 1);
    ASSERT_EQ(response->GetInstances()[0].GetId(), "instance_0");
    delete response;
  }
}

TEST_F(ConsumerApiMockServerConnectorTest, TestGetInstances) {
  ServiceKey service_key;
  GetInstancesRequest empty_service_name_request(service_key);
  InstancesResponse *response = nullptr;
  ReturnCode ret = consumer_api_->GetInstances(empty_service_name_request, response);
  ASSERT_EQ(ret, kReturnInvalidArgument);
  ASSERT_FALSE(response != nullptr);

  GetInstancesRequest request(service_key_);
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockFireEventHandler),
                           ::testing::Return(kReturnOk)));

  ret = consumer_api_->GetInstances(request, response);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(response != nullptr);
  ASSERT_EQ(response->GetInstances().size(), instance_num_);  // 隔离和权重为0的不返回
  delete response;
}

TEST_F(ConsumerApiMockServerConnectorTest, TestGetAllInstances) {
  ServiceKey service_key;
  GetInstancesRequest empty_service_name_request(service_key);
  InstancesResponse *response = nullptr;
  ReturnCode ret = consumer_api_->GetAllInstances(empty_service_name_request, response);
  ASSERT_EQ(ret, kReturnInvalidArgument);
  ASSERT_FALSE(response != nullptr);

  GetInstancesRequest request(service_key_);
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(1))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockFireEventHandler),
                           ::testing::Return(kReturnOk)));
  ASSERT_EQ(consumer_api_->GetAllInstances(request, response), kReturnOk);
  ASSERT_TRUE(response != nullptr);
  ASSERT_EQ(response->GetInstances().size(), instance_num_ + 2);  // 隔离和权重为0的也返回
  delete response;
}

TEST_F(ConsumerApiMockServerConnectorTest, TestGetInstancesTimeout) {
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockIgnoreEventHandler),
                           ::testing::Return(kReturnOk)));

  GetInstancesRequest request(service_key_);
  InstancesResponse *response = nullptr;
  ReturnCode ret = consumer_api_->GetInstances(request, response);
  ASSERT_EQ(ret, kReturnTimeout);
  ASSERT_FALSE(response != nullptr);
}

TEST_F(ConsumerApiMockServerConnectorTest, TestAsyncGetOneInstance) {
  ServiceKey service_key;
  GetOneInstanceRequest empty_service_name_request(service_key);
  InstancesFuture *future = nullptr;
  ReturnCode ret = consumer_api_->AsyncGetOneInstance(empty_service_name_request, future);
  ASSERT_EQ(ret, kReturnInvalidArgument);
  ASSERT_FALSE(future != nullptr);

  GetOneInstanceRequest request(service_key_);
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockFireEventHandler),
                           ::testing::Return(kReturnOk)));

  ret = consumer_api_->AsyncGetOneInstance(request, future);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(future != nullptr);
  InstancesResponse *response = nullptr;
  ret = future->Get(constants::kApiTimeoutDefault, response);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(response != nullptr);
  delete future;
  delete response;
}

TEST_F(ConsumerApiMockServerConnectorTest, TestAsyncGetOneInstanceTimeout) {
  GetOneInstanceRequest request(service_key_);
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockIgnoreEventHandler),
                           ::testing::Return(kReturnOk)));

  InstancesFuture *future = nullptr;
  ReturnCode ret = consumer_api_->AsyncGetOneInstance(request, future);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(future != nullptr);
  InstancesResponse *response = nullptr;
  ret = future->Get(constants::kApiTimeoutDefault, response);
  ASSERT_EQ(ret, kReturnTimeout);
  ASSERT_FALSE(response != nullptr);
  delete future;
}

TEST_F(ConsumerApiMockServerConnectorTest, TestAsyncGetInstances) {
  ServiceKey service_key;
  GetInstancesRequest empty_service_name_request(service_key);
  InstancesFuture *future = nullptr;
  ReturnCode ret = consumer_api_->AsyncGetInstances(empty_service_name_request, future);
  ASSERT_EQ(ret, kReturnInvalidArgument);
  ASSERT_FALSE(future != nullptr);

  GetInstancesRequest request(service_key_);
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockFireEventHandler),
                           ::testing::Return(kReturnOk)));

  ret = consumer_api_->AsyncGetInstances(request, future);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(future != nullptr);
  InstancesResponse *response = nullptr;
  ret = future->Get(constants::kApiTimeoutDefault, response);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(response != nullptr);
  delete future;
  delete response;
}

TEST_F(ConsumerApiMockServerConnectorTest, TestAsyncGetInstancesTimeout) {
  GetInstancesRequest request(service_key_);
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockIgnoreEventHandler),
                           ::testing::Return(kReturnOk)));

  InstancesFuture *future = nullptr;
  ReturnCode ret = consumer_api_->AsyncGetInstances(request, future);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(future != nullptr);
  InstancesResponse *response = nullptr;
  ret = future->Get(constants::kApiTimeoutDefault, response);
  ASSERT_EQ(ret, kReturnTimeout);
  ASSERT_FALSE(response != nullptr);
  delete future;
}

TEST_F(ConsumerApiMockServerConnectorTest, TestUpdateServiceCallResult) {
  instance_num_ = 1;
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockFireEventHandler),
                           ::testing::Return(kReturnOk)));

  ReturnCode ret;
  GetOneInstanceRequest request(service_key_);
  Instance instance;
  ServiceCallResult result;
  result.SetServiceNamespace(service_key_.namespace_);
  result.SetServiceName(service_key_.name_);
  result.SetDelay(100);
  // 先获取并上报一定数量成功，防止错误率插件重复熔断
  for (int i = 0; i < constants::kContinuousErrorThresholdDefault * 2; i++) {
    ret = consumer_api_->GetOneInstance(request, instance);
    ASSERT_EQ(ret, kReturnOk);
    ASSERT_EQ(instance.GetId(), "instance_0");
    if (i % 2 == 0) {
      result.SetInstanceId(instance.GetId());
    } else {
      result.SetInstanceId("");
      result.SetInstanceHostAndPort(instance.GetHost(), instance.GetPort());
    }
    result.SetRetCode(100);
    result.SetRetStatus(kCallRetOk);
    ret = consumer_api_->UpdateServiceCallResult(result);
    ASSERT_EQ(ret, kReturnOk);
  }
  for (int i = 0; i < constants::kContinuousErrorThresholdDefault; i++) {
    ret = consumer_api_->GetOneInstance(request, instance);
    ASSERT_EQ(ret, kReturnOk);
    ASSERT_EQ(instance.GetId(), "instance_0");
    if (i % 2 == 0) {
      result.SetInstanceId(instance.GetId());
    } else {
      result.SetInstanceId("");
      result.SetInstanceHostAndPort(instance.GetHost(), instance.GetPort());
    }
    result.SetRetCode(-100);
    result.SetRetStatus(kCallRetError);
    ret = consumer_api_->UpdateServiceCallResult(result);
    ASSERT_EQ(ret, kReturnOk);
  }
  // 只有一个实例且被熔断，由于路由模块最少返回实例比例不满足依然会返回该实例
  ret = consumer_api_->GetOneInstance(request, instance);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(instance.GetId(), "instance_0");

  // 半开后请求
  sleep(constants::kHalfOpenSleepWindowDefault / 1000 + 1);
  for (int i = 0; i < constants::kRequestCountAfterHalfOpenDefault; i++) {
    ret = consumer_api_->GetOneInstance(request, instance);
    ASSERT_EQ(ret, kReturnOk);
    ASSERT_EQ(instance.GetId(), "instance_0");
    ServiceCallResult result;
    result.SetServiceNamespace(service_key_.namespace_);
    result.SetServiceName(service_key_.name_);
    if (i % 2 == 0) {
      result.SetInstanceId(instance.GetId());
    } else {
      result.SetInstanceId("");
      result.SetInstanceHostAndPort(instance.GetHost(), instance.GetPort());
    }
    result.SetDelay(10);
    result.SetRetCode(0);
    result.SetRetStatus(kCallRetOk);
    ret = consumer_api_->UpdateServiceCallResult(result);
    ASSERT_EQ(ret, kReturnOk);
  }
  // 恢复正常
  sleep(1);
  ret = consumer_api_->GetOneInstance(request, instance);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(instance.GetId(), "instance_0");
}

TEST_F(ConsumerApiMockServerConnectorTest, TestGetRouteRuleKeys) {
  const std::set<std::string> *keys = nullptr;
  (*routing_response_.mutable_routing()->add_inbounds()->add_sources()->mutable_metadata())["key1"];
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(1))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockFireEventHandler),
                           ::testing::Return(kReturnOk)));

  ASSERT_EQ(consumer_api_->GetRouteRuleKeys(service_key_, 1000, keys), kReturnOk);
  ASSERT_TRUE(keys->count("key1") == 1);
}

struct CohashFactor {
  double totalDiff;
  double stdDev;
  double deviation;
};

class ConsumerApiRingHashMockServerConnectorTest : public ConsumerApiMockServerConnectorTest {
 protected:
  virtual void SetUp() {
    MockServerConnectorTest::SetUp();
    context_ = nullptr;
    consumer_api_ = nullptr;
    TestUtils::CreateTempDir(persist_dir_);
    std::string err_msg, content = GetConfig();

    Config *config = Config::CreateFromString(content, err_msg);
    POLARIS_ASSERT(config != nullptr && err_msg.empty());

    context_ = Context::Create(config);
    ASSERT_TRUE(context_ != nullptr);
    delete config;
    ASSERT_TRUE((consumer_api_ = ConsumerApi::Create(context_)) != nullptr);

    // check
    MockServerConnector *server_connector_in_context =
        dynamic_cast<MockServerConnector *>(context_->GetContextImpl()->GetServerConnector());
    ASSERT_TRUE(server_connector_ != nullptr);
    ASSERT_EQ(server_connector_, server_connector_in_context);
    service_key_.name_ = "cpp_test_service";
    service_key_.namespace_ = "cpp_test_namespace";
    instance_num_ = 10;
    instance_healthy_ = true;
  }

  virtual std::string GetConfig() {
    std::string content =
        "global:\n"
        "  serverConnector:\n"
        "    protocol: " +
        server_connector_plugin_name_ +
        "\nconsumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        persist_dir_ + "\n" +
        "  setCircuitBreaker:\n"
        "    enable: true\n" +
        "  loadBalancer:\n"
        "    type: ringHash\n"
        "    vnodeCount: 10\n";
    return content;
  }

  void InitServiceData(bool bRandomWeight) {
    FakeServer::InstancesResponse(instances_response_, service_key_);
    v1::Service *service = instances_response_.mutable_service();
    for (int i = 0; i < 10; i++) {
      (*service->mutable_metadata())["key" + std::to_string(i)] = "value" + std::to_string(i);
    }
    instance_num_ = 1000;  // 1000 个节点
    srand(time(nullptr));
    for (int i = 0; i < instance_num_; i++) {
      ::v1::Instance *instance = instances_response_.mutable_instances()->Add();
      instance->mutable_namespace_()->set_value(service_key_.namespace_);
      instance->mutable_service()->set_value(service_key_.name_);
      instance->mutable_id()->set_value("instance_" + std::to_string(i));
      instance->mutable_host()->set_value("host" + std::to_string(i));
      instance->mutable_port()->set_value(i);
      instance->mutable_healthy()->set_value(instance_healthy_);
      uint32_t weight = 100;
      if (bRandomWeight) {
        weight += 50 - rand() % 100;
      }
      instance->mutable_weight()->set_value(weight);
    }
    FakeServer::RoutingResponse(routing_response_, service_key_);
  }

  void Prepare(bool bRandomWeight = false) {
    InitServiceData(bRandomWeight);
    EXPECT_CALL(*server_connector_, RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_,
                                                         ::testing::_, ::testing::_))
        .Times(::testing::Exactly(2))
        .WillRepeatedly(
            ::testing::DoAll(::testing::Invoke(this, &ConsumerApiMockServerConnectorTest::MockFireEventHandler),
                             ::testing::Return(kReturnOk)));
  }

  void TestLoadBalancerDeviation(const std::string &cohashMethod, double stdDev, uint32_t invokeCnt = 100000) {
    std::map<std::string, int> instanceHits;
    srand(time(nullptr));
    Hash64Func hashFunc = nullptr;
    ASSERT_EQ(HashManager::Instance().GetHashFunction("murmur3", hashFunc), kReturnOk);
    char buff[128];
    GetOneInstanceRequest request(service_key_);
    Instance instance;
    for (uint32_t i = 0; i < invokeCnt; ++i) {
      memset(buff, 0, sizeof(buff));
      snprintf(buff, sizeof(buff), "test_hashkey_%d", rand());
      uint64_t key = hashFunc(buff, strlen(buff), 0);
      request.SetHashKey(key);
      ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
      std::string instID = instance.GetId();
      ASSERT_TRUE(!instID.empty());
      instanceHits[instID]++;
    }

    CohashFactor factor = CalcCohashFactor(instanceHits);
    printf(
        "CohashFactorTest.%s"
        "\n\t\033[32;m hitInstances=%lu totalDiff=%f deviation=%f "
        "stdDev=%.3f\033[0m\n",
        cohashMethod.c_str(), instanceHits.size(), factor.totalDiff, factor.deviation, factor.stdDev);
    ASSERT_TRUE(factor.stdDev < stdDev);
  }

  CohashFactor CalcCohashFactor(const std::map<std::string, int> &instanceHits) {
    polaris::GetInstancesRequest req(service_key_);
    polaris::InstancesResponse *pRsp = nullptr;
    consumer_api_->GetInstances(req, pRsp);
    std::vector<polaris::Instance> &vctInsts = pRsp->GetInstances();
    CohashFactor factor;
    factor.totalDiff = CalcTotalDiff(vctInsts, instanceHits);
    factor.deviation = CalcDeviation(vctInsts, instanceHits);
    factor.stdDev = sqrtl(factor.deviation);
    delete pRsp;
    return factor;
  }

  double CalcDeviation(std::vector<polaris::Instance> &vctInsts, const std::map<std::string, int> &instanceHits) {
    std::map<std::string, polaris::Instance> mapInsts;
    for (uint32_t i = 0; i < vctInsts.size(); ++i) {
      mapInsts[vctInsts[i].GetId()] = vctInsts[i];
    }
    double totalNormHits = 0;
    std::vector<double> hitsPerWeight;
    std::map<std::string, int>::const_iterator it = instanceHits.begin();
    for (; it != instanceHits.end(); ++it) {
      double normHits = static_cast<double>(it->second) / static_cast<double>(mapInsts[it->first].GetWeight());
      totalNormHits += normHits;
      hitsPerWeight.push_back(normHits);
    }
    double avgHits = totalNormHits / static_cast<double>(hitsPerWeight.size());
    totalNormHits = 0;
    std::vector<double>::iterator itHPW = hitsPerWeight.begin();
    for (; itHPW != hitsPerWeight.end(); ++itHPW) {
      totalNormHits += pow(*itHPW - avgHits, 2);
    }
    totalNormHits /= static_cast<double>(hitsPerWeight.size());
    return totalNormHits;
  }

  double CalcTotalDiff(std::vector<polaris::Instance> &vctInsts, const std::map<std::string, int> &instanceHits) {
    uint64_t totalWeight = 0;
    std::map<std::string, polaris::Instance> mapInsts;
    for (uint32_t i = 0; i < vctInsts.size(); ++i) {
      polaris::Instance &inst = vctInsts[i];
      totalWeight += inst.GetWeight();
      mapInsts[inst.GetId()] = inst;
    }
    uint64_t totalHits = 0;
    std::map<std::string, int>::const_iterator it = instanceHits.begin();
    for (; it != instanceHits.end(); ++it) {
      totalHits += it->second;
    }
    double totalDiff = 0;
    double maxDiff = 0;
    for (it = instanceHits.begin(); it != instanceHits.end(); ++it) {
      double weightBP = static_cast<double>(mapInsts[it->first].GetWeight()) / static_cast<double>(totalWeight);
      double hitBP = static_cast<double>(it->second) / static_cast<double>(totalHits);
      double diff = fabs(weightBP - hitBP);
      if (diff > maxDiff) {
        maxDiff = diff;
      }
      totalDiff += diff;
      // printf("instance %s| weight: %u/%lu(total), hits: %u/%lu(total)\n", it->first.c_str(),
      //        mapInsts[it->first].GetWeight(), totalWeight, it.second, totalHits);
    }
    printf("Max diff %f\n", maxDiff);
    return totalDiff;
  }
};

TEST_F(ConsumerApiRingHashMockServerConnectorTest, TestGetOneInstanceCohashRequest) {
  ServiceKey service_key;
  GetOneInstanceRequest empty_service_name_request(service_key);
  Instance instance;
  ASSERT_EQ(consumer_api_->GetOneInstance(empty_service_name_request, instance), kReturnInvalidArgument);
  ASSERT_TRUE(instance.GetId().empty());
  InstancesResponse *response = nullptr;
  ASSERT_EQ(consumer_api_->GetOneInstance(empty_service_name_request, response), kReturnInvalidArgument);
  ASSERT_FALSE(response != nullptr);

  GetOneInstanceRequest request(service_key_);
  request.SetHashKey(100);
  Prepare(false);  // prepare instance data

  ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
  ASSERT_TRUE(!instance.GetId().empty());
  std::string instID = instance.GetId();
  ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
  ASSERT_TRUE(!instance.GetId().empty());
  ASSERT_EQ(instance.GetId(), instID);
  ASSERT_EQ(consumer_api_->GetOneInstance(request, response), kReturnOk);
  ASSERT_TRUE(response != nullptr);
  delete response;
}

TEST_F(ConsumerApiRingHashMockServerConnectorTest, TestRingHashDeviationUniformWeightRequest) {
  Prepare(false);
  TestLoadBalancerDeviation("ringhash-uniform-weight", 0.5);
}

TEST_F(ConsumerApiRingHashMockServerConnectorTest, TestRingHashDeviationRandomWeightRequest) {
  Prepare(true);
  TestLoadBalancerDeviation("ringhash-random-weight", 0.5);
}

TEST_F(ConsumerApiRingHashMockServerConnectorTest, TestRingHashWithoutKeyDevaitionUniformWeightRequest) {
  Prepare(false);
  TestLoadBalancerDeviation("ringhashNoKey-uniform-weight", 0.5);
}

TEST_F(ConsumerApiRingHashMockServerConnectorTest, TestRingHashWithoutKeyDevaitionRandomWeightRequest) {
  Prepare(true);
  TestLoadBalancerDeviation("ringhashNoKey-random-weight", 0.5);
}

TEST_F(ConsumerApiRingHashMockServerConnectorTest, TestRingHashWithHashStringRequest) {
  ServiceKey service_key;
  GetOneInstanceRequest empty_service_name_request(service_key);
  Instance instance;
  ASSERT_EQ(consumer_api_->GetOneInstance(empty_service_name_request, instance), kReturnInvalidArgument);
  ASSERT_TRUE(instance.GetId().empty());
  InstancesResponse *response = nullptr;
  ASSERT_EQ(consumer_api_->GetOneInstance(empty_service_name_request, response), kReturnInvalidArgument);
  ASSERT_FALSE(response != nullptr);

  GetOneInstanceRequest request(service_key_);
  request.SetHashString("polaris-ringhash-hashstring-one");
  Prepare(false);

  ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
  ASSERT_TRUE(!instance.GetId().empty());
  std::string instID = instance.GetId();
  ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
  ASSERT_TRUE(!instance.GetId().empty());
  ASSERT_EQ(instance.GetId(), instID);
  request.SetHashString("polaris-ringhash-hashstring-two");
  Instance instance2;
  ASSERT_EQ(consumer_api_->GetOneInstance(request, instance2), kReturnOk);
  ASSERT_TRUE(!instance2.GetId().empty());
  ASSERT_NE(instance2.GetId(), instID);
  ASSERT_EQ(consumer_api_->GetOneInstance(request, response), kReturnOk);
  ASSERT_TRUE(response != nullptr);
  delete response;
}

class ConsumerApiMaglevMockServerConnectorTest : public ConsumerApiRingHashMockServerConnectorTest {
 protected:
  virtual std::string GetConfig() {
    std::string content =
        "global:\n"
        "  serverConnector:\n"
        "    protocol: " +
        server_connector_plugin_name_ +
        "\nconsumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        persist_dir_ + "\n" +
        "  setCircuitBreaker:\n"
        "    enable: true\n" +
        "  loadBalancer:\n"
        "    type: maglev\n";
    // "    vnodeCount: 10\n";
    return content;
  }
};

TEST_F(ConsumerApiMaglevMockServerConnectorTest, TestGetOneInstanceMaglevRequest) {
  ServiceKey service_key;
  GetOneInstanceRequest empty_service_name_request(service_key);
  Instance instance;
  ASSERT_EQ(consumer_api_->GetOneInstance(empty_service_name_request, instance), kReturnInvalidArgument);
  ASSERT_TRUE(instance.GetId().empty());
  InstancesResponse *response = nullptr;
  ASSERT_EQ(consumer_api_->GetOneInstance(empty_service_name_request, response), kReturnInvalidArgument);
  ASSERT_FALSE(response != nullptr);

  GetOneInstanceRequest request(service_key_);
  request.SetHashKey(100);
  Prepare(false);

  ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
  ASSERT_TRUE(!instance.GetId().empty());
  std::string instID = instance.GetId();
  ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
  ASSERT_TRUE(!instance.GetId().empty());
  ASSERT_EQ(instance.GetId(), instID);
  ASSERT_EQ(consumer_api_->GetOneInstance(request, response), kReturnOk);
  ASSERT_TRUE(response != nullptr);
  delete response;
}

TEST_F(ConsumerApiMaglevMockServerConnectorTest, TestMaglevDeviationUniformWeightRequest) {
  Prepare(false);
  TestLoadBalancerDeviation("maglev-uniform-weight", 0.2);
}

TEST_F(ConsumerApiMaglevMockServerConnectorTest, TestMaglevDeviationRandomWeightRequest) {
  Prepare(true);
  TestLoadBalancerDeviation("maglev-random-weight", 0.2);
}

TEST_F(ConsumerApiMaglevMockServerConnectorTest, TestMaglevWithoutKeyDeviationUniformWeightRequest) {
  Prepare(false);
  TestLoadBalancerDeviation("maglevNoKey-uniform-weight", 0.2);
}

TEST_F(ConsumerApiMaglevMockServerConnectorTest, TestMaglevWithoutKeyDeviationRandomWeightRequest) {
  Prepare(true);
  TestLoadBalancerDeviation("maglevNoKey-random-weight", 0.2);
}

}  // namespace polaris
