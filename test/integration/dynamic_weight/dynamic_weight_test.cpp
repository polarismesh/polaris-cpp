//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
//  language governing permissions and limitations under the License.
//

#include <gtest/gtest.h>

#include "integration/common/environment.h"
#include "integration/common/http_client.h"
#include "integration/common/integration_base.h"
#include "polaris/consumer.h"
#include "polaris/provider.h"
#include "utils/time_clock.h"

#include <vector>
using namespace std;

namespace polaris {

class TestApiBase {
 protected:
  void CreateContext() {
    std::string err_msg;
    std::string config_string =
        "global:\n"
        "  serverConnector:\n"
        "    addresses: [" +
        Environment::GetDiscoverServer() +
        "]\nconsumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        Environment::GetPersistDir() +
        "\n  circuitBreaker:\n"
        "    setCircuitBreaker:\n"
        "      enable: true\n"
        "\ndynamic_weight:\n"
        "  isOpenDynamicWeight: true\n";
    Config* config = Config::CreateFromString(config_string, err_msg);
    ASSERT_TRUE(config != nullptr) << config_string;
    context_ = Context::Create(config, kShareContext);
    ASSERT_TRUE(context_ != nullptr);
    delete config;
  }

  void DestroyContext() {
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
  }

 protected:
  Context* context_;
};

class TestProviderApi : public TestApiBase {
 public:
  TestProviderApi(v1::Service& service, string& service_token, int port)
      : shared_service_(service),
        shared_service_token_(service_token),
        port_(port),
        provider_(nullptr),
        instance_id_("") {}

  virtual void SetUp() {
    // 1. create context
    CreateContext();

    // 2. create provider
    provider_ = ProviderApi::Create(context_);
    ASSERT_TRUE(provider_ != nullptr);

    // 3. register instance
    ServiceKey service_key = {shared_service_.namespace_().value(), shared_service_.name().value()};
    InstanceRegisterRequest register_request(service_key.namespace_, service_key.name_, shared_service_token_,
                                             "127.0.0.1", port_);
    register_request.SetWeight(200);
    ASSERT_EQ(provider_->Register(register_request, instance_id_), kReturnOk);
    ASSERT_FALSE(instance_id_.empty());

    cout << "register instance. namespace: " << service_key.namespace_.c_str()
         << ", name: " << service_key.name_.c_str() << ", token: " << shared_service_token_ << ", port: " << port_
         << ", instance_id: " << instance_id_ << endl;
  }

  virtual void TearDown() {
    // deregister
    if (!instance_id_.empty()) {
      InstanceDeregisterRequest deregister_request(shared_service_token_, instance_id_);
      ASSERT_EQ(provider_->Deregister(deregister_request), kReturnOk);
    }

    // delete provider
    if (provider_ != nullptr) {
      delete provider_;
    }

    // destroy context
    DestroyContext();
  }

  polaris::ProviderApi* GetProviderApi() { return provider_; }

  int GetPort() const { return port_; }
  std::string GetInstanceID() const { return instance_id_; }

 protected:
  v1::Service& shared_service_;
  std::string& shared_service_token_;
  int port_;

  ProviderApi* provider_;
  std::string instance_id_;
};

class TestConsumerApi : public TestApiBase {
 public:
  TestConsumerApi(v1::Service& service, string& service_token)
      : shared_service_(service), shared_service_token_(service_token), consumer_(nullptr) {}

  virtual void SetUp() {
    // 1. create context
    CreateContext();

    // 2. create consumer
    consumer_ = polaris::ConsumerApi::Create(context_);
    ASSERT_TRUE(consumer_ != nullptr);
  }

  virtual void TearDown() {
    // delete consumer
    if (consumer_ != nullptr) {
      delete consumer_;
    }

    // destroy context
    DestroyContext();
  }

  polaris::ConsumerApi* GetConsumerApi() { return consumer_; }

 protected:
  v1::Service& shared_service_;
  std::string& shared_service_token_;

  polaris::ConsumerApi* consumer_;
};

class DynamicWeightTest : public IntegrationBase {
 public:
  virtual void SetUp() {
    // create service
    string service_name_ = "provider.api.dwtest" + std::to_string(Time::GetSystemTimeMs());
    string service_namespace_ = "Test";
    service_.mutable_namespace_()->set_value(service_namespace_);
    service_.mutable_name()->set_value(service_name_);
    // cout << "create service: " << service_name_ << endl;
    // CreateService(service_, service_token_);  // create service

    IntegrationBase::SetUp();

    sleep(3);  // wait for create service ok

    // add dynamicweight config
    stringstream ss;
    ss << "[{";
    ss << "\"service\": \"" << service_name_ << "\",";
    ss << "\"namespace\": \"" << service_namespace_ << "\",";
    ss << "\"isEnable\": true,";
    ss << "\"interval\": 3,";
    ss << "\"service_token\": \"" << service_token_ << "\"";
    ss << "}]";
    config_request_ = std::string(ss.str());
    std::cout << config_request_ << std::endl;
    std::string response;
    HttpClient::DoRequest(HTTP_POST, "/naming/v1/dynamicweight", config_request_, 1000, response);

    // add 2 providers
    providers_.push_back(make_shared<TestProviderApi>(service_, service_token_, 8081));
    providers_.push_back(make_shared<TestProviderApi>(service_, service_token_, 8082));
    for (auto provider : providers_) {
      provider->SetUp();
    }

    sleep(2);  // wait for name svr syncing

    // add 1 consumers
    consumers_.push_back(make_shared<TestConsumerApi>(service_, service_token_));
    for (auto consumer : consumers_) {
      consumer->SetUp();
    }
  }

  virtual void TearDown() {
    std::string response;
    HttpClient::DoRequest(HTTP_POST, "/naming/v1/dynamicweight/delete", config_request_, 1000, response);

    for (auto consumer : consumers_) {
      consumer->TearDown();
    }

    for (auto provider : providers_) {
      provider->TearDown();
    }

    if (!service_token_.empty()) {
      // delete service
      DeleteService(service_.name().value(), service_.namespace_().value(), service_token_);
    }
  }

 protected:
  std::string config_request_;
  vector<shared_ptr<TestProviderApi> > providers_;
  vector<shared_ptr<TestConsumerApi> > consumers_;
};

TEST_F(DynamicWeightTest, TestNormalCase) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  polaris::GetInstancesRequest request(service_key);
  polaris::InstancesResponse* response;
  ASSERT_EQ(consumers_[0]->GetConsumerApi()->GetInstances(request, response), kReturnOk);
  cout << "get all instance. namespace: " << service_key.namespace_.c_str() << ", name: " << service_key.name_.c_str()
       << ", token: " << service_token_ << ", vec[0].ip: " << response->GetInstances()[0].GetHost().c_str()
       << ", vec[0].port: " << response->GetInstances()[0].GetPort()
       << ", vec[1].ip: " << response->GetInstances()[1].GetHost().c_str()
       << ", vec[1].port: " << response->GetInstances()[1].GetPort() << endl;
  ASSERT_EQ(response->GetInstances().size(), 2);
  delete response;
}

TEST_F(DynamicWeightTest, ReportHalfFull) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  polaris::GetInstancesRequest request(service_key);

  // report average weight
  for (int i = 0; i < 5; i++) {
    for (size_t i = 0; i < providers_.size(); i++) {
      std::map<std::string, std::string> metric;
      metric["capacity"] = "100";
      metric["used"] = std::to_string(50);
      polaris::DynamicWeightRequest dynamicweight_req(service_key.namespace_, service_key.name_, service_token_,
                                                      "127.0.0.1", providers_[i]->GetPort());
      dynamicweight_req.SetMetrics(metric);
      providers_[i]->GetProviderApi()->ReportDynamicWeight(dynamicweight_req);
    }
    sleep(1);
  }

  polaris::InstancesResponse* response;
  ASSERT_EQ(consumers_[0]->GetConsumerApi()->GetInstances(request, response), kReturnOk);
  cout << "get all instance. namespace: " << service_key.namespace_.c_str() << ", name: " << service_key.name_.c_str()
       << ", token: " << service_token_ << ", vec[0].ip: " << response->GetInstances()[0].GetHost().c_str()
       << ", vec[0].port: " << response->GetInstances()[0].GetPort()
       << ", vec[1].ip: " << response->GetInstances()[1].GetHost().c_str()
       << ", vec[1].port: " << response->GetInstances()[1].GetPort() << endl;
  ASSERT_EQ(response->GetInstances().size(), 2);

  // check dynamic weight
  EXPECT_EQ(response->GetInstances()[0].GetDynamicWeight(), response->GetInstances()[1].GetDynamicWeight());
  delete response;
}

// test for capacity
TEST_F(DynamicWeightTest, ReportEmptyFull) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};

  // report average weight
  for (int i = 0; i < 5; i++) {
    for (size_t i = 0; i < providers_.size(); i++) {
      std::map<std::string, std::string> metric;
      metric["capacity"] = "100";
      metric["used"] = std::to_string(providers_[i]->GetPort() == 8081 ? 0 : 100);
      polaris::DynamicWeightRequest dynamicweight_req(service_key.namespace_, service_key.name_, service_token_,
                                                      "127.0.0.1", providers_[i]->GetPort());
      dynamicweight_req.SetMetrics(metric);
      providers_[i]->GetProviderApi()->ReportDynamicWeight(dynamicweight_req);
    }
    sleep(1);
  }

  // check only the instance of port 8081 is avaiable
  {
    // trigger dynamic weight timer updating task
    polaris::GetOneInstanceRequest request(service_key);
    request.SetLoadBalanceType(polaris::kLoadBalanceTypeDynamicWeighted);
    polaris::Instance instance;
    consumers_[0]->GetConsumerApi()->GetOneInstance(request, instance);
    sleep(5);
  }

  for (int i = 0; i < 10; i++) {
    // keep reporting
    for (size_t i = 0; i < providers_.size(); i++) {
      std::map<std::string, std::string> metric;
      metric["capacity"] = "100";
      metric["used"] = std::to_string(providers_[i]->GetPort() == 8081 ? 0 : 100);
      polaris::DynamicWeightRequest dynamicweight_req(service_key.namespace_, service_key.name_, service_token_,
                                                      "127.0.0.1", providers_[i]->GetPort());
      dynamicweight_req.SetMetrics(metric);
      providers_[i]->GetProviderApi()->ReportDynamicWeight(dynamicweight_req);
    }

    // check dynamic weight
    {
      polaris::GetInstancesRequest request(service_key);
      polaris::InstancesResponse* response;
      ASSERT_EQ(consumers_[0]->GetConsumerApi()->GetInstances(request, response), kReturnOk);

      cout << "get all instance. namespace: " << service_key.namespace_.c_str()
           << ", name: " << service_key.name_.c_str() << ", token: " << service_token_
           << ", vec[0].ip: " << response->GetInstances()[0].GetHost().c_str()
           << ", vec[0].port: " << response->GetInstances()[0].GetPort()
           << ", vec[0].dynamicweight: " << response->GetInstances()[0].GetDynamicWeight()
           << ", vec[1].ip: " << response->GetInstances()[1].GetHost().c_str()
           << ", vec[1].port: " << response->GetInstances()[1].GetPort()
           << ", vec[1].dynamicweight: " << response->GetInstances()[1].GetDynamicWeight() << endl;

      if (response->GetInstances()[0].GetPort() == 8081) {
        EXPECT_EQ(response->GetInstances()[0].GetDynamicWeight(), 100);
        EXPECT_EQ(response->GetInstances()[1].GetDynamicWeight(), 0);
      } else {
        EXPECT_EQ(response->GetInstances()[0].GetDynamicWeight(), 0);
        EXPECT_EQ(response->GetInstances()[1].GetDynamicWeight(), 100);
      }
      delete response;
    }

    // check expected instance
    {
      polaris::GetOneInstanceRequest request(service_key);
      request.SetLoadBalanceType(polaris::kLoadBalanceTypeDynamicWeighted);
      polaris::Instance instance;
      EXPECT_EQ(consumers_[0]->GetConsumerApi()->GetOneInstance(request, instance), kReturnOk);
      EXPECT_EQ(instance.GetPort(), 8081);
    }

    sleep(1);
  }
}

TEST_F(DynamicWeightTest, ReportFullFull) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};

  {
    // trigger dynamic weight timer updating task
    polaris::GetOneInstanceRequest request(service_key);
    request.SetLoadBalanceType(polaris::kLoadBalanceTypeDynamicWeighted);
    polaris::Instance instance;
    consumers_[0]->GetConsumerApi()->GetOneInstance(request, instance);
    sleep(1);
  }

  // report average weight
  for (int i = 0; i < 5; i++) {
    for (size_t i = 0; i < providers_.size(); i++) {
      std::map<std::string, std::string> metric;
      metric["capacity"] = "100";
      metric["used"] = "100";
      providers_[i]->GetProviderApi()->ReportDynamicWeightWithID(providers_[i]->GetInstanceID(), metric);
    }
    sleep(5);
  }

  {
    polaris::GetInstancesRequest request(service_key);
    polaris::InstancesResponse* response;
    ASSERT_EQ(consumers_[0]->GetConsumerApi()->GetInstances(request, response), kReturnOk);

    cout << "get all instance. namespace: " << service_key.namespace_.c_str() << ", name: " << service_key.name_.c_str()
         << ", token: " << service_token_ << ", vec[0].ip: " << response->GetInstances()[0].GetHost().c_str()
         << ", vec[0].port: " << response->GetInstances()[0].GetPort()
         << ", vec[0].dynamicweight: " << response->GetInstances()[0].GetDynamicWeight()
         << ", vec[1].ip: " << response->GetInstances()[1].GetHost().c_str()
         << ", vec[1].port: " << response->GetInstances()[1].GetPort()
         << ", vec[1].dynamicweight: " << response->GetInstances()[1].GetDynamicWeight() << endl;

    // 退化为静态权重
    EXPECT_EQ(response->GetInstances()[0].GetDynamicWeight(), 200);
    EXPECT_EQ(response->GetInstances()[1].GetDynamicWeight(), 200);

    delete response;
    sleep(1);
  }
}

TEST_F(DynamicWeightTest, ReportEmptyEmpty) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  {
    // trigger dynamic weight timer updating task
    polaris::GetOneInstanceRequest request(service_key);
    request.SetLoadBalanceType(polaris::kLoadBalanceTypeDynamicWeighted);
    polaris::Instance instance;
    consumers_[0]->GetConsumerApi()->GetOneInstance(request, instance);
    sleep(1);
  }

  // report average weight
  for (int i = 0; i < 5; i++) {
    for (size_t i = 0; i < providers_.size(); i++) {
      std::map<std::string, std::string> metric;
      metric["capacity"] = "100";
      metric["used"] = "0";
      polaris::DynamicWeightRequest dynamicweight_req(service_key.namespace_, service_key.name_, service_token_,
                                                      "127.0.0.1", providers_[i]->GetPort());
      dynamicweight_req.SetMetrics(metric);
      providers_[i]->GetProviderApi()->ReportDynamicWeight(dynamicweight_req);
    }
    sleep(5);
  }

  {
    polaris::GetInstancesRequest request(service_key);
    polaris::InstancesResponse* response;
    ASSERT_EQ(consumers_[0]->GetConsumerApi()->GetInstances(request, response), kReturnOk);

    cout << "get all instance. namespace: " << service_key.namespace_.c_str() << ", name: " << service_key.name_.c_str()
         << ", token: " << service_token_ << ", vec[0].ip: " << response->GetInstances()[0].GetHost().c_str()
         << ", vec[0].port: " << response->GetInstances()[0].GetPort()
         << ", vec[0].dynamicweight: " << response->GetInstances()[0].GetDynamicWeight()
         << ", vec[1].ip: " << response->GetInstances()[1].GetHost().c_str()
         << ", vec[1].port: " << response->GetInstances()[1].GetPort()
         << ", vec[1].dynamicweight: " << response->GetInstances()[1].GetDynamicWeight() << endl;

    EXPECT_EQ(response->GetInstances()[0].GetDynamicWeight(), 100);
    EXPECT_EQ(response->GetInstances()[1].GetDynamicWeight(), 100);

    delete response;
    sleep(1);
  }
}

TEST_F(DynamicWeightTest, ReportTimeout) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};

  {
    // trigger dynamic weight timer updating task
    polaris::GetOneInstanceRequest request(service_key);
    request.SetLoadBalanceType(polaris::kLoadBalanceTypeDynamicWeighted);
    polaris::Instance instance;
    consumers_[0]->GetConsumerApi()->GetOneInstance(request, instance);
    sleep(1);
  }

  // report average weight
  for (int i = 0; i < 5; i++) {
    for (size_t j = 0; j < providers_.size(); j++) {
      std::map<std::string, std::string> metric;
      metric["capacity"] = "100";
      metric["used"] = "50";
      polaris::DynamicWeightRequest dynamicweight_req(service_key.namespace_, service_key.name_, service_token_,
                                                      "127.0.0.1", providers_[j]->GetPort());
      dynamicweight_req.SetMetrics(metric);
      providers_[j]->GetProviderApi()->ReportDynamicWeight(dynamicweight_req);
    }
    sleep(5);
  }

  {
    polaris::GetInstancesRequest request(service_key);
    polaris::InstancesResponse* response;
    ASSERT_EQ(consumers_[0]->GetConsumerApi()->GetInstances(request, response), kReturnOk);

    cout << "get all instance. namespace: " << service_key.namespace_.c_str() << ", name: " << service_key.name_.c_str()
         << ", token: " << service_token_ << ", vec[0].ip: " << response->GetInstances()[0].GetHost().c_str()
         << ", vec[0].port: " << response->GetInstances()[0].GetPort()
         << ", vec[0].dynamicweight: " << response->GetInstances()[0].GetDynamicWeight()
         << ", vec[1].ip: " << response->GetInstances()[1].GetHost().c_str()
         << ", vec[1].port: " << response->GetInstances()[1].GetPort()
         << ", vec[1].dynamicweight: " << response->GetInstances()[1].GetDynamicWeight() << endl;

    EXPECT_EQ(response->GetInstances()[0].GetDynamicWeight(), 100);
    EXPECT_EQ(response->GetInstances()[1].GetDynamicWeight(), 100);
  }

  // report only one
  for (int i = 0; i < 13; i++) {
    std::map<std::string, std::string> metric;
    metric["capacity"] = "100";
    metric["used"] = "50";
    polaris::DynamicWeightRequest dynamicweight_req(service_key.namespace_, service_key.name_, service_token_,
                                                    "127.0.0.1", providers_[0]->GetPort());
    dynamicweight_req.SetMetrics(metric);
    providers_[0]->GetProviderApi()->ReportDynamicWeight(dynamicweight_req);
    sleep(1);
  }

  {
    polaris::GetInstancesRequest request(service_key);
    polaris::InstancesResponse* response;
    ASSERT_EQ(consumers_[0]->GetConsumerApi()->GetInstances(request, response), kReturnOk);

    cout << "get all instance. namespace: " << service_key.namespace_.c_str() << ", name: " << service_key.name_.c_str()
         << ", token: " << service_token_ << ", vec[0].ip: " << response->GetInstances()[0].GetHost().c_str()
         << ", vec[0].port: " << response->GetInstances()[0].GetPort()
         << ", vec[0].dynamicweight: " << response->GetInstances()[0].GetDynamicWeight()
         << ", vec[1].ip: " << response->GetInstances()[1].GetHost().c_str()
         << ", vec[1].port: " << response->GetInstances()[1].GetPort()
         << ", vec[1].dynamicweight: " << response->GetInstances()[1].GetDynamicWeight() << endl;

    if (response->GetInstances()[0].GetPort() == providers_[0]->GetPort()) {
      EXPECT_EQ(response->GetInstances()[0].GetDynamicWeight(), 100);
      EXPECT_EQ(response->GetInstances()[1].GetDynamicWeight(), 100);
    } else {
      EXPECT_EQ(response->GetInstances()[0].GetDynamicWeight(), 100);
      EXPECT_EQ(response->GetInstances()[1].GetDynamicWeight(), 100);
    }

    delete response;
    sleep(1);
  }
}

TEST_F(DynamicWeightTest, DynamicCreateInstance) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};

  {
    // trigger dynamic weight timer updating task
    polaris::GetOneInstanceRequest request(service_key);
    request.SetLoadBalanceType(polaris::kLoadBalanceTypeDynamicWeighted);
    polaris::Instance instance;
    consumers_[0]->GetConsumerApi()->GetOneInstance(request, instance);
    sleep(1);
  }

  // report average weight
  for (int i = 0; i < 5; i++) {
    for (size_t j = 0; j < providers_.size(); j++) {
      std::map<std::string, std::string> metric;
      metric["capacity"] = "100";
      metric["used"] = "50";
      polaris::DynamicWeightRequest dynamicweight_req(service_key.namespace_, service_key.name_, service_token_,
                                                      "127.0.0.1", providers_[j]->GetPort());
      dynamicweight_req.SetMetrics(metric);
      providers_[j]->GetProviderApi()->ReportDynamicWeight(dynamicweight_req);
    }
    sleep(5);
  }

  {
    polaris::GetInstancesRequest request(service_key);
    polaris::InstancesResponse* response;
    ASSERT_EQ(consumers_[0]->GetConsumerApi()->GetInstances(request, response), kReturnOk);

    cout << "get all instance. namespace: " << service_key.namespace_.c_str() << ", name: " << service_key.name_.c_str()
         << ", token: " << service_token_ << ", vec[0].ip: " << response->GetInstances()[0].GetHost().c_str()
         << ", vec[0].port: " << response->GetInstances()[0].GetPort()
         << ", vec[0].dynamicweight: " << response->GetInstances()[0].GetDynamicWeight()
         << ", vec[1].ip: " << response->GetInstances()[1].GetHost().c_str()
         << ", vec[1].port: " << response->GetInstances()[1].GetPort()
         << ", vec[1].dynamicweight: " << response->GetInstances()[1].GetDynamicWeight() << endl;

    EXPECT_EQ(response->GetInstances()[0].GetDynamicWeight(), 100);
    EXPECT_EQ(response->GetInstances()[1].GetDynamicWeight(), 100);
  }

  // add new instance
  auto provider = make_shared<TestProviderApi>(service_, service_token_, 8083);
  provider->SetUp();
  providers_.push_back(provider);

  for (int i = 0; i < 10; i++) {
    for (size_t j = 0; j < providers_.size(); j++) {
      std::map<std::string, std::string> metric;
      metric["capacity"] = "100";
      metric["used"] = "50";
      polaris::DynamicWeightRequest dynamicweight_req(service_key.namespace_, service_key.name_, service_token_,
                                                      "127.0.0.1", providers_[j]->GetPort());
      dynamicweight_req.SetMetrics(metric);
      providers_[j]->GetProviderApi()->ReportDynamicWeight(dynamicweight_req);
    }
    sleep(1);
  }

  {
    polaris::GetInstancesRequest request(service_key);
    polaris::InstancesResponse* response;
    ASSERT_EQ(consumers_[0]->GetConsumerApi()->GetInstances(request, response), kReturnOk);

    cout << "get all instance. namespace: " << service_key.namespace_.c_str() << ", name: " << service_key.name_.c_str()
         << ", token: " << service_token_ << ", vec[0].ip: " << response->GetInstances()[0].GetHost().c_str()
         << ", vec[0].port: " << response->GetInstances()[0].GetPort()
         << ", vec[0].dynamicweight: " << response->GetInstances()[0].GetDynamicWeight()
         << ", vec[1].ip: " << response->GetInstances()[1].GetHost().c_str()
         << ", vec[1].port: " << response->GetInstances()[1].GetPort()
         << ", vec[1].dynamicweight: " << response->GetInstances()[1].GetDynamicWeight() << endl;

    EXPECT_EQ(response->GetInstances()[0].GetDynamicWeight(), 100);
    EXPECT_EQ(response->GetInstances()[1].GetDynamicWeight(), 100);
    EXPECT_EQ(response->GetInstances()[2].GetDynamicWeight(), 100);

    delete response;
    sleep(1);
  }
}

TEST_F(DynamicWeightTest, DynamicDeleteInstance) {}

TEST_F(DynamicWeightTest, DynamicServerPartFail) {}

TEST_F(DynamicWeightTest, DynamicServerAllFail) {}

}  // namespace polaris
