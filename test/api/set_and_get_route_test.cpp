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

#include "api/consumer_api.h"
#include "context/context_impl.h"
#include "mock/fake_server_response.h"
#include "mock/mock_server_connector.h"
#include "polaris/consumer.h"
#include "polaris/plugin.h"
#include "test_utils.h"
#include "utils/file_utils.h"
#include "utils/time_clock.h"

namespace polaris {

static const std::string remote_route_rule =
    "{\"code\":200000,\"info\":\"execute "
    "success\",\"type\":\"ROUTING\",\"service\":{\"name\":\"test\",\"namespace\":\"Test\","
    "\"revision\":\"2bdb2e16ff9a4441a415d754bbe020b1\"},\"routing\":{\"service\":\"test\","
    "\"namespace\":\"Test\",\"outbounds\":[{\"sources\":[{\"service\":\"test\",\"namespace\":"
    "\"Test\",\"metadata\":{\"env\":{\"value\":\"base\"}}}],\"destinations\":[{\"service\":\"*"
    "\",\"namespace\":\"Test\",\"metadata\":{\"env\":{\"value\":\"base\"}},\"priority\":0,"
    "\"weight\":100,\"isolate\":false},{\"service\":\"*\",\"namespace\":\"Test\",\"metadata\":{"
    "\"env\":{\"value\":\"test\"}},\"priority\":0,\"weight\":100,\"isolate\":false}]}]}}";

static const std::string local_route_rule =
    "{\"code\":200000,\"info\":\"execute "
    "success\",\"type\":\"ROUTING\",\"service\":{\"name\":\"test\",\"namespace\":\"Test\","
    "\"revision\":\"35e2f45dae654c619e80d1de9f9a024a\"},\"routing\":{\"service\":\"test\","
    "\"namespace\":\"Test\",\"outbounds\":[{\"sources\":[{\"service\":\"test\",\"namespace\":"
    "\"Test\",\"metadata\":{\"env\":{\"value\":\"base\"}}}],\"destinations\":[{\"service\":\"*\","
    "\"namespace\":\"Test\",\"metadata\":{\"env\":{\"value\":\"base\"}},\"priority\":0,"
    "\"weight\":100,\"isolate\":false}]}]}}";

class SetAndGetRouteMockServerConnectorTest : public MockServerConnectorTest {
 protected:
  virtual void SetUp() {
    MockServerConnectorTest::SetUp();
    context_ = nullptr;
    consumer_api_ = nullptr;
    json_route_rule_ = remote_route_rule;
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
    service_key_.namespace_ = "Test";
    service_key_.name_ = "test";
    instance_healthy_ = true;
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
    (*service->mutable_metadata())["env"] = "base";

    for (int i = 1; i <= 3; ++i) {
      ::v1::Instance *instance = instances_response_.mutable_instances()->Add();
      instance->mutable_namespace_()->set_value(service_key_.namespace_);
      instance->mutable_service()->set_value(service_key_.name_);
      instance->mutable_id()->set_value("instance_" + std::to_string(i));
      instance->mutable_host()->set_value("host_" + std::to_string(i));
      instance->mutable_port()->set_value(8000 + i);
      instance->mutable_healthy()->set_value(instance_healthy_);
      instance->mutable_weight()->set_value(100);
      if (i == 1) {
        google::protobuf::Map<std::string, std::string> *metadata_map = instance->mutable_metadata();
        (*metadata_map)["env"] = "base";
      }
      if (i == 2) {
        google::protobuf::Map<std::string, std::string> *metadata_map = instance->mutable_metadata();
        (*metadata_map)["env"] = "test";
      }
    }
  }

 public:
  void MockFireEventHandler(const ServiceKey &service_key, ServiceDataType data_type, uint64_t /*sync_interval*/,
                            const std::string & /*disk_revision*/, ServiceEventHandler *handler) {
    ServiceData *service_data = nullptr;
    if (data_type == kServiceDataInstances) {
      service_data = ServiceData::CreateFromPb(&instances_response_, kDataIsSyncing);
    } else if (data_type == kServiceDataRouteRule) {
      service_data = ServiceData::CreateFromJson(json_route_rule_, kDataIsSyncing, Time::GetSystemTimeMs());
    } else {
      EXPECT_TRUE(false) << "unexpected data type:" << data_type;
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
  std::string json_route_rule_;
  v1::DiscoverResponse instances_response_;
  ServiceKey service_key_;
  bool instance_healthy_;
  std::string persist_dir_;
  std::vector<pthread_t> event_thread_list_;
};

TEST_F(SetAndGetRouteMockServerConnectorTest, TestSetAndGetRoute) {
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &SetAndGetRouteMockServerConnectorTest::MockFireEventHandler),
                           ::testing::Return(kReturnOk)));

  ReturnCode ret;
  Instance instance;
  GetOneInstanceRequest request(service_key_);
  polaris::ServiceInfo service_info;
  std::map<int, int> instance_map;

  // 有三个实例，instance_1、instance_2、instance_3
  // 根据远端的路由规则进行过滤，只会获得instance_1和instance_2
  service_info.service_key_ = service_key_;
  service_info.metadata_["env"] = "base";
  request.SetSourceService(service_info);

  for (int i = 0; i < 1000; i++) {
    ret = consumer_api_->GetOneInstance(request, instance);
    ASSERT_EQ(ret, kReturnOk);
    instance_map[instance.GetPort()] += 1;
  }
  ASSERT_TRUE(instance_map[8001] > 0);
  ASSERT_TRUE(instance_map[8002] > 0);
  ASSERT_TRUE(instance_map[8003] == 0);

  // 获取远端的路由规则
  std::string route_rule;
  for (int i = 0; i < 1000; i++) {
    ret = consumer_api_->GetServiceRouteRule(service_key_, 500, route_rule);
    ASSERT_EQ(ret, kReturnOk);
  }
  ASSERT_EQ(route_rule, json_route_rule_);

  // 根据本地的路由规则进行过滤，只会获得instance_1
  ServiceData *local_service_data = ServiceData::CreateFromJson(local_route_rule, kDataIsSyncing, 0);
  TrpcInstanceRequestInfo req;
  req.service_key = &service_key_;
  req.source_service = &service_info;
  req.source_route_rule_service_data = local_service_data;

  for (int i = 0; i < 1000; i++) {
    TrpcInstancesResponseInfo resp;
    ret = ConsumerApiImpl::TrpcGetOneInstance(context_, req, resp);
    std::vector<Instance> &instances = resp.instances;
    ASSERT_EQ(instances.size(), 1);
    Instance instance = instances[0];
    ASSERT_TRUE(instance.GetPort() == 8001);
  }
  local_service_data->DecrementRef();
}

// 测试tRPC多环境路由
TEST_F(SetAndGetRouteMockServerConnectorTest, TestTrpcRoute) {
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &SetAndGetRouteMockServerConnectorTest::MockFireEventHandler),
                           ::testing::Return(kReturnOk)));

  ReturnCode ret;
  GetInstancesRequest instances_request(service_key_);
  polaris::ServiceInfo service_info;

  // 获取远端的路由规则
  ServiceData *remote_service_data = nullptr;
  ret = ConsumerApiImpl::TrpcGetServiceServiceData(context_, service_key_, 500, remote_service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(remote_service_data != nullptr);
  std::string remote_route_rule = remote_service_data->ToJsonString();
  ASSERT_EQ(remote_route_rule, json_route_rule_);
  remote_service_data->DecrementRef();

  // 根据本地的路由规则进行过滤，只会获得instance_1
  service_info.service_key_ = service_key_;
  service_info.metadata_["env"] = "base";

  ServiceData *local_service_data = ServiceData::CreateFromJson(local_route_rule, kDataIsSyncing, 0);
  for (int i = 0; i < 100; i++) {
    TrpcInstanceRequestInfo req;
    req.service_key = &service_key_;
    req.source_service = &service_info;
    req.source_route_rule_service_data = local_service_data;

    TrpcInstancesResponseInfo resp;
    ret = ConsumerApiImpl::TrpcGetOneInstance(context_, req, resp);
    ASSERT_EQ(ret, kReturnOk);
    std::vector<Instance> &instances = resp.instances;
    ASSERT_EQ(instances.size(), 1);
    Instance instance = instances[0];
    ASSERT_TRUE(instance.GetPort() == 8001);
  }

  instances_request.SetSourceService(service_info);
  for (int i = 0; i < 100; i++) {
    InstancesResponse *response = nullptr;
    ret = ConsumerApiImpl::TrpcGetInstances(context_, instances_request, local_service_data, response);
    ASSERT_EQ(ret, kReturnOk);
    std::vector<Instance> &instances = response->GetInstances();
    ASSERT_EQ(instances.size(), 1);
    Instance instance = instances[0];
    ASSERT_TRUE(instance.GetPort() == 8001);
    delete response;
  }
  // ASSERT_EQ(local_service_data->DecrementAndGetRef(), 0);
  local_service_data->DecrementRef();
}

// 测试tRPC多环境路由信息的维护
TEST(EnvTransInfoTest, Build) {
  ServiceKey source_service_key;
  source_service_key.name_ = "test.service";
  source_service_key.namespace_ = "Dev";
  std::string env_trans_info = "base1, base2";
  ServiceData *source_service_data = nullptr;
  // 通过透传信息构造ServiceData
  source_service_data = ConsumerApiImpl::BuildServiceData(source_service_key, env_trans_info);
  ASSERT_TRUE(source_service_data != nullptr);

  // 通过ServiceData重建透传信息
  ServiceInfo source_service_info;
  source_service_info.service_key_ = source_service_key;
  std::string env_result;
  ConsumerApiImpl::BuildEnvTransInfo(&source_service_info, source_service_data, env_result);
  ASSERT_EQ(env_result, "base1,base2");  //字符串中的空格被自动剔除了
  ASSERT_EQ(source_service_data->DecrementAndGetRef(), 0);
}

}  // namespace polaris