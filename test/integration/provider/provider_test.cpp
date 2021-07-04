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
#include "utils/string_utils.h"
#include "utils/time_clock.h"

namespace polaris {

class ProviderApiTest : public IntegrationBase {
protected:
  virtual void SetUp() {
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("provider.api.test" +
                                       StringUtils::TypeToStr(Time::GetCurrentTimeMs()));
    IntegrationBase::SetUp();
    sleep(3);
    // 创建Provider对象
    provider_ = ProviderApi::Create(context_);
    ASSERT_TRUE(provider_ != NULL);
  }

  virtual void TearDown() {
    if (provider_ != NULL) {
      delete provider_;
    }
    IntegrationBase::TearDown();
  }

protected:
  ProviderApi* provider_;
};

TEST_F(ProviderApiTest, TestRegistInstance) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  InstanceRegisterRequest register_request(service_key.namespace_, service_key.name_,
                                           service_token_, "127.0.0.1", 8088);
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

TEST_F(ProviderApiTest, TestDisableHeartbeat) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  InstanceRegisterRequest register_request(service_key.namespace_, service_key.name_,
                                           service_token_, "127.0.0.1", 8088);
  std::string instance_id;
  ASSERT_EQ(provider_->Register(register_request, instance_id), kReturnOk);
  ASSERT_FALSE(instance_id.empty());

  InstanceHeartbeatRequest heartbeat_request(service_token_, instance_id);
  sleep(2);
  ASSERT_EQ(provider_->Heartbeat(heartbeat_request), kReturnHealthyCheckDisable);

  polaris::InstanceDeregisterRequest deregister_request(service_token_, instance_id);
  ASSERT_EQ(provider_->Deregister(deregister_request), kReturnOk);
}

TEST_F(ProviderApiTest, TestHeartbeat) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  InstanceRegisterRequest register_request(service_key.namespace_, service_key.name_,
                                           service_token_, "127.0.0.1", 8088);
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

TEST_F(ProviderApiTest, TestDeregister) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  InstanceRegisterRequest register_request(service_key.namespace_, service_key.name_,
                                           service_token_, "127.0.0.1", 8088);
  std::string instance_id;
  ASSERT_EQ(provider_->Register(register_request, instance_id), kReturnOk);
  ASSERT_FALSE(instance_id.empty());

  polaris::InstanceDeregisterRequest deregister_request(service_key.namespace_, service_key.name_,
                                                        service_token_, "127.0.0.1", 8088);
  for (int i = 0; i < 3; ++i) {
    sleep(2);
    ASSERT_EQ(provider_->Deregister(deregister_request), kReturnOk);
  }
}

}  // namespace polaris