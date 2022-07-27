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

#include "context/context_impl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "context/service_context.h"
#include "mock/fake_server_response.h"
#include "plugin/load_balancer/ringhash/ringhash.h"

namespace polaris {

class ServiceContextTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    context_ = nullptr;
    config_ = nullptr;
  }

  virtual void TearDown() {
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
    if (config_ != nullptr) {
      delete config_;
      config_ = nullptr;
    }
  }

 protected:
  Config* config_;
  Context* context_;
};

TEST_F(ServiceContextTest, TestServiceLevelConfig) {
  std::string err_msg;
  std::string content = R"##(
global:
  serverConnector:
    addresses:
    - 127.0.0.1:8091
consumer:
  service:
    - name: polaris.cpp.sdk.test1
      namespace: Test
      loadBalancer:
        type: ringHash
        vnodeCount: 1024
        hashFunc: murmur3 
    - name: polaris.cpp.sdk.test2
      namespace: Test
      loadBalancer:
        type: ringHash
        vnodeCount: 10240 
)##";

  config_ = Config::CreateFromString(content, err_msg);
  ASSERT_TRUE(config_ != nullptr && err_msg.empty());
  context_ = Context::Create(config_);
  ASSERT_TRUE(context_ != nullptr);

  auto context_impl = context_->GetContextImpl();
  ServiceKey service_key = {"Test", "polaris.cpp.sdk.test1"};
  auto service_context = context_impl->GetServiceContext(service_key);
  ASSERT_TRUE(service_context != nullptr);

  service_key = {"Test", "polaris.cpp.sdk.test2"};
  service_context = context_impl->GetServiceContext(service_key);
  ASSERT_TRUE(service_context != nullptr);

  auto lb = service_context->GetLoadBalancer(kLoadBalanceTypeDefaultConfig);
  auto ring_hash = dynamic_cast<KetamaLoadBalancer*>(lb);
  ASSERT_TRUE(ring_hash != nullptr);
}

TEST_F(ServiceContextTest, TestHealthCheckConfig) {
  std::string err_msg;
  std::string content = R"##(
global:
  serverConnector:
    addresses:
    - 127.0.0.1:8091    
consumer:
  healthCheck:
    when: always
  service:
    - name: polaris.cpp.sdk.test
      namespace: Test
      healthCheck:
        when: never
)##";

  config_ = Config::CreateFromString(content, err_msg);
  ASSERT_TRUE(config_ != nullptr && err_msg.empty());
  context_ = Context::Create(config_);
  ASSERT_TRUE(context_ != nullptr);

  auto context_impl = context_->GetContextImpl();
  auto service_context = context_impl->GetServiceContext({"Test", "polaris.cpp.sdk.test"});
  ASSERT_TRUE(service_context != nullptr);
  auto health_chain = service_context->GetHealthCheckerChain();
  ASSERT_EQ(health_chain->GetWhen(), "never");

  context_impl = context_->GetContextImpl();
  service_context = context_impl->GetServiceContext({"Test", "polaris.cpp.sdk.test2"});
  ASSERT_TRUE(service_context != nullptr);
  health_chain = service_context->GetHealthCheckerChain();
  ASSERT_EQ(health_chain->GetWhen(), "always");
}

TEST_F(ServiceContextTest, TestServiceLevelDegrade) {
  std::string err_msg;
  std::string content = R"##(
global:
  serverConnector:
    addresses:
    - 127.0.0.1:8091    
consumer:
  serviceRouter:
    enable: true
    chain:  
      - nearbyBasedRouter
  healthCheck:
    when: always
  circuitBreaker:
    chain:
      - errorCount
  service:
    - name: polaris.cpp.sdk.test
      namespace: Test
      loadBalancer:
        type: ringHash
        vnodeCount: 1024
        hashFunc: murmur3 
)##";

  config_ = Config::CreateFromString(content, err_msg);
  ASSERT_TRUE(config_ != nullptr && err_msg.empty());
  context_ = Context::Create(config_);
  ASSERT_TRUE(context_ != nullptr);

  auto context_impl = context_->GetContextImpl();
  ServiceKey service_key = {"Test", "polaris.cpp.sdk.test"};
  auto service_context = context_impl->GetServiceContext(service_key);
  ASSERT_TRUE(service_context != nullptr);

  auto lb = service_context->GetLoadBalancer(kLoadBalanceTypeDefaultConfig);
  ASSERT_EQ(lb->GetLoadBalanceType(), "ringHash");
  auto route_chain = service_context->GetServiceRouterChain();
  ASSERT_EQ(route_chain->IsRuleRouterEnable(), false);
  auto health_chain = service_context->GetHealthCheckerChain();
  ASSERT_EQ(health_chain->GetWhen(), "always");
  auto circuit_chain = service_context->GetCircuitBreakerChain();
  ASSERT_EQ(circuit_chain->GetCircuitBreakers().size(), 1);
}

TEST_F(ServiceContextTest, TestInstanceExistChecker) {
  config_ = Config::CreateEmptyConfig();
  context_ = Context::Create(config_);
  ASSERT_TRUE(context_ != nullptr);

  ServiceKey service_key = {"Test", "polaris.cpp.sdk.test"};
  auto context_impl = context_->GetContextImpl();
  auto service_context = context_impl->GetServiceContext(service_key);
  ASSERT_TRUE(service_context != nullptr);

  for (int i = 0; i < 5; ++i) {
    ASSERT_FALSE(service_context->CheckInstanceExist("instance_" + std::to_string(i)));
  }

  v1::DiscoverResponse response;
  FakeServer::CreateServiceInstances(response, service_key, 5);
  ServiceData* service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  ASSERT_TRUE(service_data != nullptr);

  service_context->UpdateInstances(service_data);

  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(service_context->CheckInstanceExist("instance_" + std::to_string(i)));
  }

  for (int i = 5; i < 10; ++i) {
    ASSERT_FALSE(service_context->CheckInstanceExist("instance_" + std::to_string(i)));
  }

  service_data->DecrementRef();
}

}  // namespace polaris
