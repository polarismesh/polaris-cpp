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

#include "integration/common/environment.h"
#include "integration/common/integration_base.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

namespace polaris {

class CanaryRouterTest : public IntegrationBase {
protected:
  virtual void SetUp() {
    config_string_ =
        "global:\n"
        "  serverConnector:\n"
        "    addresses: [" +
        Environment::GetDiscoverServer() +
        "]\nconsumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        Environment::GetPersistDir() +
        "\n  serviceRouter:\n"
        "    chain:\n"
        "      - ruleBasedRouter\n"
        "      - nearbyBasedRouter\n"
        "      - canaryRouter\n";
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("canary.router.test" +
                                       StringUtils::TypeToStr(Time::GetCurrentTimeMs()));
    (*service_.mutable_metadata())["internal-canary"] = "true";
    IntegrationBase::SetUp();

    // 创建Consumer对象
    consumer_ = ConsumerApi::CreateFromString(config_string_);
    ASSERT_TRUE(consumer_ != NULL) << config_string_;
  }

  virtual void TearDown() {
    if (consumer_ != NULL) {
      delete consumer_;
    }
    for (std::size_t i = 0; i < instances_.size(); ++i) {
      DeletePolarisServiceInstance(instances_[i]);
    }
    IntegrationBase::TearDown();
  }

  void CreateInstance(const std::string& ip, uint32_t port, bool healthy,
                      const std::string& canary_value = "") {
    v1::Instance instance;
    instance.mutable_namespace_()->set_value(service_.namespace_().value());
    instance.mutable_service()->set_value(service_.name().value());
    instance.mutable_service_token()->set_value(service_token_);
    instance.mutable_weight()->set_value(100);
    instance.mutable_host()->set_value(ip);
    instance.mutable_port()->set_value(port);
    instance.mutable_healthy()->set_value(healthy);
    if (!canary_value.empty()) {
      (*instance.mutable_metadata())["canary"] = canary_value;
    }
    std::string instance_id;
    AddPolarisServiceInstance(instance, instance_id);
    instance.mutable_id()->set_value(instance_id);
    instances_.push_back(instance);
  }

  void WaitDataReady() {
    ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
    GetInstancesRequest request(service_key);
    InstancesResponse* response;
    int sleep_count = 10;
    ReturnCode ret_code;
    std::size_t instances_size = 0;
    while (sleep_count-- > 0) {
      if ((ret_code = consumer_->GetAllInstances(request, response)) == kReturnOk) {
        instances_size = response->GetInstances().size();
        delete response;
        if (instances_size == instances_.size()) {
          break;
        }
      }
      sleep(1);
    }
    ASSERT_EQ(instances_size, instances_.size());
  }

  void MakeCircuitBreaker(int index) {
    ServiceCallResult call_result;
    call_result.SetServiceNamespace(service_.namespace_().value());
    call_result.SetServiceName(service_.name().value());
    call_result.SetInstanceId(instances_[index].id().value());
    for (int i = 0; i < 11; i++) {
      call_result.SetDelay(50);
      call_result.SetRetStatus(kCallRetError);
      ASSERT_EQ(consumer_->UpdateServiceCallResult(call_result), kReturnOk);
    }
  }

protected:
  ConsumerApi* consumer_;
  std::vector<v1::Instance> instances_;
};

TEST_F(CanaryRouterTest, TestGetInstanceNotCanary) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  GetOneInstanceRequest one_instance_request(service_key);
  Instance instance;

  for (int index = 0; index < 4; index++) {
    if (index == 0) {  // v1金丝雀不健康实例
      CreateInstance("127.0.0.1", 10000 + index, false, "v1");
    } else if (index == 1) {  // 非金丝雀不健康实例
      CreateInstance("127.0.0.1", 10000 + index, false);
    } else if (index == 2) {  // v2金丝雀健康实例
      CreateInstance("127.0.0.1", 10000 + index, true, "v2");
    } else if (index == 3) {  // 非金丝雀健康实例
      CreateInstance("127.0.0.1", 10000 + index, true);
    }
    WaitDataReady();
    for (int i = 0; i < 10; i++) {
      ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk) << index;
      ASSERT_EQ(instance.GetPort(), instances_[index].port().value()) << index;
    }
  }

  // 熔断非金丝雀健康节点，返回v2金丝雀健康节点
  MakeCircuitBreaker(3);
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk);
    ASSERT_EQ(instance.GetPort(), 10002);
  }

  // 熔断v2金丝雀节点，返回2个非金丝雀节点
  MakeCircuitBreaker(2);
  GetInstancesRequest instances_request(service_key);
  InstancesResponse* response;
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(consumer_->GetInstances(instances_request, response), kReturnOk);
    ASSERT_EQ(response->GetInstances().size(), 2);
    int port = response->GetInstances()[0].GetPort();
    ASSERT_TRUE(port == 10001 || port == 10003) << port;
    port = response->GetInstances()[1].GetPort();
    ASSERT_TRUE(port == 10001 || port == 10003) << port;
    delete response;
  }
}

TEST_F(CanaryRouterTest, TestGetInstanceCanary) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  GetOneInstanceRequest one_instance_request(service_key);
  one_instance_request.SetCanary("v2");
  Instance instance;

  for (int index = 0; index < 6; index++) {
    if (index == 0) {  // v1金丝雀不健康实例
      CreateInstance("127.0.0.1", 10000 + index, false, "v1");
    } else if (index == 1) {  // 非金丝雀不健康实例
      CreateInstance("127.0.0.1", 10000 + index, false);
    } else if (index == 2) {  // v2金丝雀不健康实例
      CreateInstance("127.0.0.1", 10000 + index, false, "v2");
    } else if (index == 3) {  // v1金丝雀健康实例
      CreateInstance("127.0.0.1", 10000 + index, true, "v1");
    } else if (index == 4) {  // 非金丝雀健康实例
      CreateInstance("127.0.0.1", 10000 + index, true);
    } else if (index == 5) {  // v2金丝雀健康实例
      CreateInstance("127.0.0.1", 10000 + index, true, "v2");
    }
    WaitDataReady();
    for (int i = 0; i < 10; i++) {
      ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk) << index;
      ASSERT_EQ(instance.GetPort(), instances_[index].port().value()) << index;
    }
  }

  // 熔断金丝雀v2健康节点，返回非金丝雀健康节点
  MakeCircuitBreaker(5);
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk);
    ASSERT_EQ(instance.GetPort(), 10004);
  }

  // 熔断非金丝雀健康节点，返回金丝雀v1健康节点
  MakeCircuitBreaker(4);
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk);
    ASSERT_EQ(instance.GetPort(), 10003);
  }

  // 熔断v1金丝雀健康节点，返回2个v2金丝雀节点
  MakeCircuitBreaker(3);
  GetInstancesRequest instances_request(service_key);
  instances_request.SetCanary("v2");
  InstancesResponse* response;
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(consumer_->GetInstances(instances_request, response), kReturnOk);
    ASSERT_EQ(response->GetInstances().size(), 2);
    int port = response->GetInstances()[0].GetPort();
    ASSERT_TRUE(port == 10002 || port == 10005) << port;
    port = response->GetInstances()[1].GetPort();
    ASSERT_TRUE(port == 10002 || port == 10005) << port;
    delete response;
  }
}

}  // namespace polaris