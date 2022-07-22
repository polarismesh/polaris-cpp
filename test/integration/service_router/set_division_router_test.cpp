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
#include "utils/time_clock.h"

namespace polaris {

class SetDivisionRouterIntegrationTest : public IntegrationBase {
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
        "      - setDivisionRouter\n"
        "      - nearbyBasedRouter\n";
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("set.division.router.test" + std::to_string(Time::GetSystemTimeMs()));
    (*service_.mutable_metadata())["internal-nearby-enable"] = "true";
    IntegrationBase::SetUp();

    // 创建Consumer对象
    consumer_ = ConsumerApi::CreateFromString(config_string_);
    ASSERT_TRUE(consumer_ != nullptr) << config_string_;

    CreateInstance("127.0.0.1", 10001, true, true, "app.sz.1");
    CreateInstance("127.0.0.1", 10002, true, true, "app.sh.1");
    CreateInstance("127.0.0.1", 10003, true, false, "app.sz.1");
    CreateInstance("127.0.0.1", 10004, true, true, "app.sz.*");
    CreateInstance("127.0.0.1", 10005, true, true, "app.sz.2");
    CreateInstance("127.0.0.1", 10006, false, true, "app.sz.1");
    WaitDataReady();
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

  void CreateInstance(const std::string& ip, uint32_t port, bool healthy, bool enable_set,
                      const std::string& set_value) {
    v1::Instance instance;
    instance.mutable_namespace_()->set_value(service_.namespace_().value());
    instance.mutable_service()->set_value(service_.name().value());
    instance.mutable_service_token()->set_value(service_token_);
    instance.mutable_weight()->set_value(100);
    instance.mutable_host()->set_value(ip);
    instance.mutable_port()->set_value(port);
    instance.mutable_healthy()->set_value(healthy);
    (*instance.mutable_metadata())["internal-enable-set"] = enable_set ? "Y" : "N";
    (*instance.mutable_metadata())["internal-set-name"] = set_value;
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
      call_result.SetRetStatus(kCallRetError);
      ASSERT_EQ(consumer_->UpdateServiceCallResult(call_result), kReturnOk);
    }
    sleep(1);
  }

 protected:
  ConsumerApi* consumer_;
  std::vector<v1::Instance> instances_;
};

TEST_F(SetDivisionRouterIntegrationTest, SetDivisionRouter) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  GetOneInstanceRequest one_instance_request(service_key);
  GetInstancesRequest instances_request(service_key);
  Instance instance;
  InstancesResponse* response;

  // 只返回set:app.sz.1下的健康节点
  instances_request.SetSourceSetName("app.sz.1");
  ASSERT_EQ(consumer_->GetInstances(instances_request, response), kReturnOk);
  ASSERT_EQ(response->GetInstances().size(), 1);
  delete response;
  one_instance_request.SetSourceSetName("app.sz.1");
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk) << i;
    ASSERT_EQ(instance.GetPort(), 10001);
  }

  // 主调使用的通配set，返回所有app.sz下的所有健康节点
  instances_request.SetSourceSetName("app.sz.*");
  ASSERT_EQ(consumer_->GetInstances(instances_request, response), kReturnOk);
  ASSERT_EQ(response->GetInstances().size(), 3);
  delete response;
  one_instance_request.SetSourceSetName("app.sz.*");
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk) << i;
    ASSERT_TRUE(instance.GetPort() == 10001 || instance.GetPort() == 10004 || instance.GetPort() == 10005);
    const std::string& instance_set = instance.GetMetadata().find("internal-set-name")->second;
    ASSERT_TRUE(instance_set.find("app.sz") == 0) << instance_set;
  }

  // set内无指定的节点，返回(groupID为*)通配set里的节点
  instances_request.SetSourceSetName("app.sz.3");
  ASSERT_EQ(consumer_->GetInstances(instances_request, response), kReturnOk);
  ASSERT_EQ(response->GetInstances().size(), 1);
  delete response;
  one_instance_request.SetSourceSetName("app.sz.3");
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk) << i;
    ASSERT_EQ(instance.GetMetadata().find("internal-set-name")->second, "app.sz.*");
  }

  // set内没有节点，且没有通配set，返回empty
  one_instance_request.SetSourceSetName("app.tj.1");
  ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnInstanceNotFound);

  // 熔断实例端口10001后，降级到10001和10006
  MakeCircuitBreaker(0);
  instances_request.SetSourceSetName("app.sz.1");
  ASSERT_EQ(consumer_->GetInstances(instances_request, response), kReturnOk);
  ASSERT_EQ(response->GetInstances().size(), 2);
  delete response;
  one_instance_request.SetSourceSetName("app.sz.1");
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk) << i;
    ASSERT_TRUE(instance.GetPort() == 10001 || instance.GetPort() == 10006);
  }
  // 主调使用的通配set，返回所有app.sz下的所有健康节点
  instances_request.SetSourceSetName("app.sz.*");
  ASSERT_EQ(consumer_->GetInstances(instances_request, response), kReturnOk);
  ASSERT_EQ(response->GetInstances().size(), 2);
  delete response;
  one_instance_request.SetSourceSetName("app.sz.*");
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk) << i;
    ASSERT_TRUE(instance.GetPort() == 10004 || instance.GetPort() == 10005);
  }
}

}  // namespace polaris