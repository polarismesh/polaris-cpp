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

#include "context/context_impl.h"
#include "context/service_context.h"
#include "mock/fake_server_response.h"
#include "mock/mock_server_connector.h"
#include "polaris/consumer.h"
#include "polaris/plugin.h"
#include "test_utils.h"
#include "utils/file_utils.h"
#include "utils/time_clock.h"

namespace polaris {

// 定义负载均衡插件类型
static const polaris::LoadBalanceType kLoadBalanceTypeSelfDefine = "kLoadBalanceTypeSelfDefine";

// 定义负载均衡插件，继承LoadBalancer
class SelfDefineLoadBalancer : public polaris::LoadBalancer {
 public:
  SelfDefineLoadBalancer();
  virtual ~SelfDefineLoadBalancer();
  virtual polaris::ReturnCode Init(polaris::Config *config, polaris::Context *context);
  virtual polaris::LoadBalanceType GetLoadBalanceType() { return kLoadBalanceTypeSelfDefine; }
  virtual polaris::ReturnCode ChooseInstance(polaris::ServiceInstances *service_instances,
                                             const polaris::Criteria &criteria, polaris::Instance *&next);
};

SelfDefineLoadBalancer::SelfDefineLoadBalancer() {}

SelfDefineLoadBalancer::~SelfDefineLoadBalancer() {}

polaris::ReturnCode SelfDefineLoadBalancer::Init(polaris::Config *, polaris::Context *) { return polaris::kReturnOk; }

polaris::ReturnCode SelfDefineLoadBalancer::ChooseInstance(polaris::ServiceInstances *service_instances,
                                                           const polaris::Criteria &, polaris::Instance *&next) {
  next = nullptr;
  InstancesSet *instances_set = service_instances->GetAvailableInstances();
  std::vector<Instance *> instances = instances_set->GetInstances();
  if (instances.size() > 0) {
    next = instances[0];  // 直接返回第一个实例
    return polaris::kReturnOk;
  }
  return polaris::kReturnInstanceNotFound;
}

// 定义负载均衡插件工厂
polaris::Plugin *SelfDefineLoadBalancerFactory() { return new SelfDefineLoadBalancer(); }

class RegisterLoadBalancerTest : public MockServerConnectorTest {
 protected:
  virtual void SetUp() {
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

TEST_F(RegisterLoadBalancerTest, TestRegisterLoadBalancer) {
  instance_num_ = 200;
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(::testing::DoAll(::testing::Invoke(this, &RegisterLoadBalancerTest::MockFireEventHandler),
                                       ::testing::Return(kReturnOk)));

  ReturnCode ret;
  Instance instance;
  GetOneInstanceRequest request(service_key_);
  request.SetLoadBalanceType(kLoadBalanceTypeWeightedRandom);
  ret = consumer_api_->GetOneInstance(request, instance);
  ASSERT_EQ(ret, kReturnOk);

  ContextImpl *context_impl = context_->GetContextImpl();
  context_impl->RcuEnter();
  ServiceContext *service_context = context_impl->GetServiceContext(service_key_);
  for (int i = 0; i < 5000; ++i) {
    LoadBalancer *load_balancer = service_context->GetLoadBalancer(kLoadBalanceTypeWeightedRandom);
    ASSERT_EQ(load_balancer->GetLoadBalanceType(), kLoadBalanceTypeWeightedRandom);
    load_balancer = service_context->GetLoadBalancer(kLoadBalanceTypeLocalityAware);
    ASSERT_EQ(load_balancer->GetLoadBalanceType(), kLoadBalanceTypeLocalityAware);
    load_balancer = service_context->GetLoadBalancer(kLoadBalanceTypeSimpleHash);
    ASSERT_EQ(load_balancer->GetLoadBalanceType(), kLoadBalanceTypeSimpleHash);
    load_balancer = service_context->GetLoadBalancer(kLoadBalanceTypeSelfDefine);
    ASSERT_TRUE(load_balancer == nullptr);
  }
  context_impl->RcuExit();

  // 注册自定义的负载均衡插件
  ret = RegisterPlugin(kLoadBalanceTypeSelfDefine, kPluginLoadBalancer, SelfDefineLoadBalancerFactory);
  ASSERT_EQ(ret, kReturnOk);

  context_impl = context_->GetContextImpl();
  context_impl->RcuEnter();
  service_context = context_impl->GetServiceContext(service_key_);
  for (int i = 0; i < 5000; ++i) {
    LoadBalancer *load_balancer = service_context->GetLoadBalancer(kLoadBalanceTypeWeightedRandom);
    ASSERT_EQ(load_balancer->GetLoadBalanceType(), kLoadBalanceTypeWeightedRandom);
    load_balancer = service_context->GetLoadBalancer(kLoadBalanceTypeSimpleHash);
    ASSERT_EQ(load_balancer->GetLoadBalanceType(), kLoadBalanceTypeSimpleHash);
    load_balancer = service_context->GetLoadBalancer(kLoadBalanceTypeSelfDefine);
    ASSERT_EQ(load_balancer->GetLoadBalanceType(), kLoadBalanceTypeSelfDefine);
  }
  context_impl->RcuExit();

  for (int i = 0; i < 5000; ++i) {
    request.SetLoadBalanceType(kLoadBalanceTypeWeightedRandom);
    ret = consumer_api_->GetOneInstance(request, instance);
    ASSERT_EQ(ret, kReturnOk);
    request.SetLoadBalanceType(kLoadBalanceTypeSimpleHash);
    ret = consumer_api_->GetOneInstance(request, instance);
    ASSERT_EQ(ret, kReturnOk);
    request.SetLoadBalanceType(kLoadBalanceTypeSelfDefine);
    ret = consumer_api_->GetOneInstance(request, instance);
    ASSERT_EQ(ret, kReturnOk);
  }
}

TEST_F(RegisterLoadBalancerTest, TestLoadBalancerPluginError) {
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(::testing::DoAll(::testing::Invoke(this, &RegisterLoadBalancerTest::MockFireEventHandler),
                                       ::testing::Return(kReturnOk)));

  Instance instance;
  GetOneInstanceRequest request(service_key_);
  LoadBalanceType not_exist_lb_type;
  request.SetLoadBalanceType(not_exist_lb_type);
  ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnPluginError);

  InstancesResponse *response = nullptr;
  ASSERT_EQ(consumer_api_->GetOneInstance(request, response), kReturnPluginError);
  ASSERT_TRUE(response == nullptr);

  InstancesFuture *future = nullptr;
  ASSERT_EQ(consumer_api_->AsyncGetOneInstance(request, future), kReturnOk);
  ASSERT_TRUE(future != nullptr);
  ASSERT_EQ(future->Get(0, response), kReturnPluginError);
  ASSERT_TRUE(response == nullptr);
  delete future;
}

}  // namespace polaris