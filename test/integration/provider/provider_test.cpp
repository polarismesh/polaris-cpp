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

#include "polaris/provider.h"

#include "integration/common/integration_base.h"
#include "polaris/consumer.h"
#include "utils/time_clock.h"

namespace polaris {

enum TestProtocol { kProtocolGrpc, kProtocolTrpc };

class ProviderApiTest : public IntegrationBase, public ::testing::WithParamInterface<TestProtocol> {
 protected:
  virtual void SetUp() {
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("provider.api.test" + std::to_string(Time::GetSystemTimeMs()));
    GetParam() == kProtocolGrpc ? IntegrationBase::SetUp() : IntegrationBase::SetUpWithTrpc();
    sleep(3);
    // 创建Provider对象
    provider_ = ProviderApi::Create(context_);
    ASSERT_TRUE(provider_ != nullptr);
  }

  virtual void TearDown() {
    if (provider_ != nullptr) {
      delete provider_;
    }
    IntegrationBase::TearDown();
  }

 protected:
  ProviderApi* provider_;
};

TEST_P(ProviderApiTest, TestRegistInstance) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  InstanceRegisterRequest register_request(service_key.namespace_, service_key.name_, service_token_, "127.0.0.1",
                                           8088);
  std::string instance_id;
  ASSERT_EQ(provider_->Register(register_request, instance_id), kReturnOk);
  ASSERT_FALSE(instance_id.empty());

  sleep(2);
  std::string instance_id2;
  ASSERT_EQ(provider_->Register(register_request, instance_id2), kReturnExistedResource);
  ASSERT_EQ(instance_id, instance_id2);

  sleep(2);
  InstanceDeregisterRequest deregister_request(service_token_, instance_id);
  ASSERT_EQ(provider_->Deregister(deregister_request), kReturnOk);
}

TEST_P(ProviderApiTest, TestDisableHeartbeat) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  InstanceRegisterRequest register_request(service_key.namespace_, service_key.name_, service_token_, "127.0.0.1",
                                           8088);
  std::string instance_id;
  ASSERT_EQ(provider_->Register(register_request, instance_id), kReturnOk);
  ASSERT_FALSE(instance_id.empty());

  InstanceHeartbeatRequest heartbeat_request(service_token_, instance_id);
  sleep(2);
  ASSERT_EQ(provider_->Heartbeat(heartbeat_request), kReturnHealthyCheckDisable);

  polaris::InstanceDeregisterRequest deregister_request(service_token_, instance_id);
  ASSERT_EQ(provider_->Deregister(deregister_request), kReturnOk);
}

TEST_P(ProviderApiTest, TestHeartbeat) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  InstanceRegisterRequest register_request(service_key.namespace_, service_key.name_, service_token_, "127.0.0.1",
                                           8088);
  register_request.SetHealthCheckFlag(true);
  register_request.SetHealthCheckType(kHeartbeatHealthCheck);
  register_request.SetTtl(1);

  std::string instance_id;
  ASSERT_EQ(provider_->Register(register_request, instance_id), kReturnOk);
  ASSERT_FALSE(instance_id.empty());
  sleep(1);

  InstanceHeartbeatRequest heartbeat_request(service_token_, instance_id);
  for (int i = 0; i < 5; ++i) {
    sleep(2);
    ASSERT_EQ(provider_->Heartbeat(heartbeat_request), kReturnOk);
  }

  polaris::InstanceDeregisterRequest deregister_request(service_token_, instance_id);
  ASSERT_EQ(provider_->Deregister(deregister_request), kReturnOk);
}

class HeartbeatCallback : public ProviderCallback {
 public:
  HeartbeatCallback() {}

  virtual ~HeartbeatCallback() {}

  virtual void Response(ReturnCode code, const std::string&) { ASSERT_EQ(code, kReturnOk); }
};

TEST_P(ProviderApiTest, TestAsyncHeartbeat) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  InstanceRegisterRequest register_request(service_key.namespace_, service_key.name_, service_token_, "127.0.0.1",
                                           8088);
  register_request.SetHealthCheckFlag(true);
  register_request.SetHealthCheckType(kHeartbeatHealthCheck);
  register_request.SetTtl(1);

  std::string instance_id;
  ASSERT_EQ(provider_->Register(register_request, instance_id), kReturnOk);
  ASSERT_FALSE(instance_id.empty());
  sleep(1);

  InstanceHeartbeatRequest heartbeat_request(service_token_, instance_id);
  for (int i = 0; i < 5; ++i) {
    sleep(2);
    ASSERT_EQ(provider_->AsyncHeartbeat(heartbeat_request, new HeartbeatCallback()), kReturnOk);
  }

  polaris::InstanceDeregisterRequest deregister_request(service_token_, instance_id);
  ASSERT_EQ(provider_->Deregister(deregister_request), kReturnOk);
}

TEST_P(ProviderApiTest, TestDeregister) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  InstanceRegisterRequest register_request(service_key.namespace_, service_key.name_, service_token_, "127.0.0.1",
                                           8088);
  std::string instance_id;
  ASSERT_EQ(provider_->Register(register_request, instance_id), kReturnOk);
  ASSERT_FALSE(instance_id.empty());

  polaris::InstanceDeregisterRequest deregister_request(service_key.namespace_, service_key.name_, service_token_,
                                                        "127.0.0.1", 8088);
  for (int i = 0; i < 3; ++i) {
    sleep(2);
    ASSERT_EQ(provider_->Deregister(deregister_request), kReturnOk);
  }
}

TEST_P(ProviderApiTest, TestRegisterWithLocation) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  InstanceRegisterRequest register_request(service_key.namespace_, service_key.name_, service_token_, "127.0.0.1",
                                           8088);
  register_request.SetLocation("华南", "深圳", "生态园");
  std::string instance_id;
  ASSERT_EQ(provider_->Register(register_request, instance_id), kReturnOk);
  ASSERT_FALSE(instance_id.empty());

  ConsumerApi* consumer = ConsumerApi::Create(context_);
  ASSERT_TRUE(consumer != nullptr);
  sleep(3);
  Instance instance;
  GetOneInstanceRequest discover_req(service_key);
  ASSERT_EQ(consumer->GetOneInstance(discover_req, instance), kReturnOk);
  ASSERT_EQ(instance.GetRegion(), "华南");
  ASSERT_EQ(instance.GetZone(), "深圳");
  ASSERT_EQ(instance.GetCampus(), "生态园");

  polaris::InstanceDeregisterRequest deregister_request(service_key.namespace_, service_key.name_, service_token_,
                                                        "127.0.0.1", 8088);
  for (int i = 0; i < 3; ++i) {
    sleep(1);
    ASSERT_EQ(provider_->Deregister(deregister_request), kReturnOk);
  }
}

INSTANTIATE_TEST_CASE_P(Test, ProviderApiTest, ::testing::Values(kProtocolGrpc, kProtocolTrpc));

}  // namespace polaris