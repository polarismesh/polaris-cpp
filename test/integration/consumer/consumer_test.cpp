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

#include <map>
#include <string>
#include <vector>

#include "polaris/consumer.h"

#include "integration/common/integration_base.h"
#include "utils/time_clock.h"

namespace polaris {

enum TestProtocol { kProtocolGrpc, kProtocolTrpc };

class ConsumerApiTest : public IntegrationBase, public ::testing::WithParamInterface<TestProtocol> {
 protected:
  virtual void SetUp() {
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("consumer.api.test" + std::to_string(Time::GetSystemTimeMs()));
    GetParam() == kProtocolGrpc ? IntegrationBase::SetUp() : IntegrationBase::SetUpWithTrpc();
    CreateInstance(healthy_instance_id_, "127.0.0.1", 8080, true, false);
    std::string instance_id;
    CreateInstance(instance_id, "127.0.0.1", 8081, false, false);
    CreateInstance(instance_id, "127.0.0.1", 8082, false, true);

    sleep(3);
    // 创建Consumer对象
    consumer_ = polaris::ConsumerApi::Create(context_);
    ASSERT_TRUE(consumer_ != nullptr);
  }

  virtual void TearDown() {
    if (consumer_ != nullptr) {
      delete consumer_;
    }
    for (std::size_t i = 0; i < instances_.size(); ++i) {
      DeletePolarisServiceInstance(instances_[i]);
    }
    IntegrationBase::TearDown();
  }

  void CreateInstance(std::string& instance_id, const std::string& ip, uint32_t port, bool healthy, bool isolate) {
    v1::Instance instance;
    instance.mutable_namespace_()->set_value(service_.namespace_().value());
    instance.mutable_service()->set_value(service_.name().value());
    instance.mutable_service_token()->set_value(service_token_);
    instance.mutable_weight()->set_value(100);
    instance.mutable_host()->set_value(ip);
    instance.mutable_port()->set_value(port);
    instance.mutable_healthy()->set_value(healthy);
    instance.mutable_isolate()->set_value(isolate);
    AddPolarisServiceInstance(instance, instance_id);
    instances_.push_back(instance);
  }

 protected:
  polaris::ConsumerApi* consumer_;
  std::vector<v1::Instance> instances_;
  std::string healthy_instance_id_;
};

TEST_P(ConsumerApiTest, TestGetInstances) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  polaris::GetInstancesRequest request(service_key);
  polaris::InstancesResponse* response;
  ASSERT_EQ(consumer_->GetInstances(request, response), kReturnOk);
  ASSERT_EQ(response->GetInstances().size(), 1);
  ASSERT_EQ(response->GetInstances()[0].GetId(), healthy_instance_id_);
  ASSERT_TRUE(!response->GetRevision().empty());
  delete response;
  response = nullptr;

  request.SetIncludeUnhealthyInstances(true);
  ASSERT_EQ(consumer_->GetInstances(request, response), kReturnOk);
  ASSERT_EQ(response->GetInstances().size(), 2);
  delete response;
  response = nullptr;

  request.SetIncludeUnhealthyInstances(false);
  ASSERT_EQ(consumer_->GetInstances(request, response), kReturnOk);
  ASSERT_EQ(response->GetInstances().size(), 1);
  ASSERT_EQ(response->GetInstances()[0].GetId(), healthy_instance_id_);
  delete response;
}

TEST_P(ConsumerApiTest, TestGetAllInstances) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  polaris::GetInstancesRequest request(service_key);
  polaris::InstancesResponse* response;
  ASSERT_EQ(consumer_->GetAllInstances(request, response), kReturnOk);
  ASSERT_EQ(response->GetInstances().size(), 3);
  std::string revision = response->GetRevision();
  ASSERT_TRUE(!revision.empty());
  delete response;
  response = nullptr;
  std::string instance_id;
  CreateInstance(instance_id, "127.0.0.1", 8083, false, true);
  int sleep_count = 5;
  while (sleep_count-- > 0) {
    if (consumer_->GetAllInstances(request, response) == kReturnOk) {
      if (response->GetInstances().size() == 4) {
        ASSERT_TRUE(revision != response->GetRevision());
        sleep_count = 0;
      } else {
        sleep(1);
      }
    }
    delete response;
  }
  ASSERT_EQ(consumer_->GetAllInstances(request, response), kReturnOk);
  ASSERT_EQ(response->GetInstances().size(), 4);
  ASSERT_TRUE(revision != response->GetRevision());
  delete response;
  response = nullptr;
}

TEST_P(ConsumerApiTest, TestUpdateCallResult) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  polaris::GetOneInstanceRequest request(service_key);
  polaris::Instance instance;
  std::string instance_id;
  for (int i = 0; i < 120; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(request, instance), kReturnOk);
    polaris::ServiceCallResult call_result;
    call_result.SetServiceNamespace(service_key.namespace_);
    call_result.SetServiceName(service_key.name_);
    if (i % 2 == 0) {
      call_result.SetInstanceId(instance.GetId());
    } else {
      call_result.SetInstanceHostAndPort(instance.GetHost(), instance.GetPort());
    }
    call_result.SetDelay(50);
    call_result.SetRetStatus(kCallRetOk);
    ASSERT_EQ(consumer_->UpdateServiceCallResult(call_result), kReturnOk);
    if (i % 40 == 0) {
      CreateInstance(instance_id, "127.0.0.1", 1000 + i, true, false);
      sleep(3);
    }
  }
}

INSTANTIATE_TEST_CASE_P(Test, ConsumerApiTest, ::testing::Values(kProtocolGrpc, kProtocolTrpc));

}  // namespace polaris