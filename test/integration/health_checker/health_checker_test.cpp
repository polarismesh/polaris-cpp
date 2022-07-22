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

#include <gtest/gtest.h>
#include <pthread.h>

#include "mock/fake_net_server.h"
#include "polaris/consumer.h"
#include "utils/time_clock.h"

#include "integration/common/environment.h"
#include "integration/common/integration_base.h"

namespace polaris {

class HealthCheckerTest : public IntegrationBase {
 protected:
  HealthCheckerTest() : consumer_api_(nullptr) {}

  virtual void SetUp() {
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("cpp.integration.cl5.cst" + std::to_string(Time::GetSystemTimeMs()));
    config_string_ =
        "global:\n"
        "  serverConnector:\n"
        "    addresses: [" +
        Environment::GetDiscoverServer() +
        "]\nconsumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        Environment::GetPersistDir() +
        "\n  circuitBreaker:\n"
        "    plugin:\n"
        "      errorCount:\n"
        "        metricExpiredTime: 100\n"
        "      errorRate:\n"
        "        metricExpiredTime: 100";
    IntegrationBase::SetUp();
    CreateInstances(2);
    sleep(3);  // 等待Discover服务器获取到服务信息
  }

  virtual void TearDown() {
    if (consumer_api_ != nullptr) {
      delete consumer_api_;
      consumer_api_ = nullptr;
    }
    DeleteInstances();
    IntegrationBase::TearDown();
  }

  void CreateOutlierDetectionConsumer();

  void CreateHealthCheckerConsumer();

  void CreateInstances(int instance_num);

  void DeleteInstances();

 protected:
  ConsumerApi* consumer_api_;
  std::vector<std::string> instances_;
};

void HealthCheckerTest::CreateInstances(int instance_num) {
  std::string instance_id;
  for (int i = 0; i < instance_num; ++i) {
    v1::Instance instance;
    instance.mutable_service_token()->set_value(service_token_);
    instance.mutable_service()->set_value(service_.name().value());
    instance.mutable_namespace_()->set_value(service_.namespace_().value());
    instance.mutable_host()->set_value("127.0.0.1");
    instance.mutable_port()->set_value(8000 + i);
    IntegrationBase::AddPolarisServiceInstance(instance, instance_id);
    instances_.push_back(instance_id);
  }
}

void HealthCheckerTest::DeleteInstances() {
  for (std::size_t i = 0; i < instances_.size(); ++i) {
    IntegrationBase::DeletePolarisServiceInstance(service_token_, instances_[i]);
  }
}

void HealthCheckerTest::CreateOutlierDetectionConsumer() {
  std::string err_msg;
  std::string config_string = config_string_ +
                              "\n  outlierDetection:\n"
                              "    enable: true\n"
                              "    checkPeriod: 2s\n"
                              "    chain:\n"
                              "    - tcp\n"
                              "    plugin:\n"
                              "      tcp:\n"
                              "        timeout: 100\n"
                              "        retry: 0";
  Config* config = Config::CreateFromString(config_string, err_msg);
  ASSERT_TRUE(config != nullptr) << config_string;
  context_ = Context::Create(config, kShareContext);
  ASSERT_TRUE(context_ != nullptr);
  delete config;
  consumer_api_ = ConsumerApi::Create(context_);
  ASSERT_TRUE(consumer_api_ != nullptr);
}

void HealthCheckerTest::CreateHealthCheckerConsumer() {
  std::string err_msg;
  std::string config_string = config_string_ +
                              "\n  healthCheck:\n"
                              "    when: always\n"
                              "    interval: 1s\n"
                              "    chain:\n"
                              "    - tcp\n"
                              "    plugin:\n"
                              "      tcp:\n"
                              "        timeout: 100\n"
                              "        retry: 0";
  Config* config = Config::CreateFromString(config_string, err_msg);
  ASSERT_TRUE(config != nullptr) << config_string;
  context_ = Context::Create(config, kShareContext);
  ASSERT_TRUE(context_ != nullptr);
  delete config;
  consumer_api_ = ConsumerApi::Create(context_);
  ASSERT_TRUE(consumer_api_ != nullptr);
}

TEST_F(HealthCheckerTest, TcpDetectorOnRecover) {
  CreateOutlierDetectionConsumer();
  ServiceKey service_key;
  service_key.namespace_ = service_.namespace_().value();
  service_key.name_ = service_.name().value();
  GetOneInstanceRequest request(service_key);
  request.SetHashKey(12345);
  request.SetLoadBalanceType(kLoadBalanceTypeL5CstHash);
  ServiceCallResult call_result;
  call_result.SetServiceNamespace(service_key.namespace_);
  call_result.SetServiceName(service_key.name_);
  Instance instance;
  ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
  NetServerParam server;
  server.port_ = instance.GetPort();
  server.status_ = kNetServerInit;

  // 触发熔断
  int port_before = instance.GetPort();
  for (int i = 0; i < 10; ++i) {
    ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    ASSERT_EQ(port_before, instance.GetPort());
    call_result.SetInstanceId(instance.GetId());
    call_result.SetRetStatus(kCallRetError);
    consumer_api_->UpdateServiceCallResult(call_result);
  }
  sleep(1);

  // 熔断后
  int port_after = 0;
  for (int i = 0; i < 100; ++i) {
    ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    if (port_after == 0) {
      port_after = instance.GetPort();
    }
    ASSERT_EQ(port_after, instance.GetPort());
    ASSERT_NE(port_before, port_after);
    call_result.SetInstanceId(instance.GetId());
    call_result.SetRetStatus(kCallRetOk);
    consumer_api_->UpdateServiceCallResult(call_result);
    usleep(30 * 1000);
  }

  // 启动服务器 网络探测成功
  ASSERT_EQ(pthread_create(&server.tid_, nullptr, FakeNetServer::StartTcp, &server), 0);
  sleep(4);
  server.status_ = kNetServerStop;
  ASSERT_EQ(pthread_join(server.tid_, nullptr), 0);

  // 半开，重新熔断
  int port_before_count = 0;
  for (int i = 0; i < 300; ++i) {
    ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    if (port_before == instance.GetPort()) {
      port_before_count++;
      call_result.SetInstanceId(instance.GetId());
      call_result.SetRetStatus(kCallRetError);
      consumer_api_->UpdateServiceCallResult(call_result);
    }
    usleep(100 * 1000);
  }
  ASSERT_EQ(port_before_count, 3);

  // 启动服务器 网络探测成功
  server.status_ = kNetServerInit;
  ASSERT_EQ(pthread_create(&server.tid_, nullptr, FakeNetServer::StartTcp, &server), 0);
  sleep(4);

  // 半开
  port_before_count = 0;
  for (int i = 0; i < 300; ++i) {
    ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    if (port_before == instance.GetPort()) {
      port_before_count++;
    }
    usleep(100 * 1000);
    call_result.SetInstanceId(instance.GetId());
    call_result.SetRetStatus(kCallRetOk);
    consumer_api_->UpdateServiceCallResult(call_result);
  }
  ASSERT_GT(port_before_count, 10) << port_before_count;
  server.status_ = kNetServerStop;
  ASSERT_EQ(pthread_join(server.tid_, nullptr), 0);
}

TEST_F(HealthCheckerTest, TcpDetectorAlways) {
  CreateHealthCheckerConsumer();

  ServiceKey service_key;
  service_key.namespace_ = service_.namespace_().value();
  service_key.name_ = service_.name().value();
  GetOneInstanceRequest request(service_key);
  Instance instance;

  ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);

  NetServerParam server1;
  server1.port_ = 8000;
  server1.status_ = kNetServerInit;

  // 启动服务器1 网络探测成功
  ASSERT_EQ(pthread_create(&server1.tid_, nullptr, FakeNetServer::StartTcp, &server1), 0);
  sleep(3);

  std::set<int> port_set;
  for (int i = 0; i < 300; ++i) {
    ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    port_set.insert(instance.GetPort());
  }
  ASSERT_EQ(port_set.size(), 1);
  ASSERT_EQ(port_set.count(8000), 1);

  // 启动服务器2 网络探测成功
  NetServerParam server2;
  server2.port_ = 8001;
  server2.status_ = kNetServerInit;
  ASSERT_EQ(pthread_create(&server2.tid_, nullptr, FakeNetServer::StartTcp, &server2), 0);
  sleep(3);
  port_set.clear();
  for (int i = 0; i < 300; ++i) {
    ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    port_set.insert(instance.GetPort());
  }
  ASSERT_EQ(port_set.size(), 2);

  // 停掉服务器1
  server1.status_ = kNetServerStop;
  ASSERT_EQ(pthread_join(server1.tid_, nullptr), 0);
  sleep(3);
  port_set.clear();
  for (int i = 0; i < 300; ++i) {
    ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    port_set.insert(instance.GetPort());
  }
  ASSERT_EQ(port_set.size(), 1);
  ASSERT_EQ(port_set.count(8001), 1);

  // 停掉服务器2
  server2.status_ = kNetServerStop;
  ASSERT_EQ(pthread_join(server2.tid_, nullptr), 0);
  sleep(3);
  port_set.clear();
  for (int i = 0; i < 300; ++i) {
    ASSERT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    port_set.insert(instance.GetPort());
  }
  ASSERT_EQ(port_set.size(), 2);  // 全死全活
}

}  // namespace polaris
