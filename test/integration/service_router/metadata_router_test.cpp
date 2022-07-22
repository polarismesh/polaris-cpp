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

class MetadataRouterTest : public IntegrationBase {
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
        "      - dstMetaRouter\n"
        "      - nearbyBasedRouter\n";
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("metadata.router.test" + std::to_string(Time::GetSystemTimeMs()));
    IntegrationBase::SetUp();

    // 创建Consumer对象
    consumer_ = ConsumerApi::CreateFromString(config_string_);
    ASSERT_TRUE(consumer_ != nullptr) << config_string_;
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

  void CreateInstance(const std::string& ip, uint32_t port, bool healthy, const std::string& metadata_value = "") {
    v1::Instance instance;
    instance.mutable_namespace_()->set_value(service_.namespace_().value());
    instance.mutable_service()->set_value(service_.name().value());
    instance.mutable_service_token()->set_value(service_token_);
    instance.mutable_weight()->set_value(100);
    instance.mutable_host()->set_value(ip);
    instance.mutable_port()->set_value(port);
    instance.mutable_healthy()->set_value(healthy);
    if (!metadata_value.empty()) {
      (*instance.mutable_metadata())["key"] = metadata_value;
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
    sleep(1);
  }

 protected:
  ConsumerApi* consumer_;
  std::vector<v1::Instance> instances_;
};

TEST_F(MetadataRouterTest, TestGetInstance) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  GetOneInstanceRequest one_instance_request(service_key);
  Instance instance;
  std::map<std::string, std::string> metadata;
  metadata["key"] = "v1";
  one_instance_request.SetMetadata(metadata);

  for (int index = 0; index < 4; index++) {
    if (index == 0) {  // v1不健康实例
      CreateInstance("127.0.0.1", 10000 + index, false, "v1");
    } else if (index == 1) {  // 不健康实例
      CreateInstance("127.0.0.1", 10000 + index, false);
    } else if (index == 2) {  // v2健康实例
      CreateInstance("127.0.0.1", 10000 + index, true, "v2");
    } else if (index == 3) {  // v1健康实例
      CreateInstance("127.0.0.1", 10000 + index, true, "v1");
    }
    WaitDataReady();
    for (int i = 0; i < 10; i++) {
      ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk) << index;
      ASSERT_EQ(instance.GetPort(), instances_[index / 3 * 3].port().value()) << index;
    }
  }

  // 熔断v1健康节点，返回v1不健康节点
  MakeCircuitBreaker(3);
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk);
    ASSERT_TRUE(instance.GetPort() == 10000 || instance.GetPort() == 10003);
  }

  metadata["key"] = "v2";
  one_instance_request.SetMetadata(metadata);
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk);
    ASSERT_EQ(instance.GetPort(), 10002);
  }

  metadata["key"] = "v3";
  one_instance_request.SetMetadata(metadata);
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnInstanceNotFound);
  }
}

TEST_F(MetadataRouterTest, TestGetInstanceFailover) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  GetOneInstanceRequest one_instance_request(service_key);
  Instance instance;
  std::map<std::string, std::string> metadata;
  metadata["key"] = "v2";
  one_instance_request.SetMetadata(metadata);

  // v1不健康实例
  CreateInstance("127.0.0.1", 10000, false, "v1");
  WaitDataReady();
  ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnInstanceNotFound);
  for (int i = 0; i < 10; i++) {
    one_instance_request.SetMetadataFailover(kMetadataFailoverNone);
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnInstanceNotFound);
    one_instance_request.SetMetadataFailover(kMetadataFailoverNotKey);
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnInstanceNotFound);
    one_instance_request.SetMetadataFailover(kMetadataFailoverAll);
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk);
  }
  // 无元数据健康实例
  CreateInstance("127.0.0.1", 10001, true, "");
  WaitDataReady();
  for (int i = 0; i < 10; i++) {
    one_instance_request.SetMetadataFailover(kMetadataFailoverNone);
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnInstanceNotFound);
    one_instance_request.SetMetadataFailover(kMetadataFailoverNotKey);
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk);
    ASSERT_EQ(instance.GetPort(), 10001);
    one_instance_request.SetMetadataFailover(kMetadataFailoverAll);
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance), kReturnOk);
    ASSERT_EQ(instance.GetPort(), 10001);
  }

  // v1健康实例
  CreateInstance("127.0.0.1", 10002, true, "v1");
  WaitDataReady();
  metadata.clear();
  metadata["key2"] = "v2";
  GetInstancesRequest instances_req(service_key);
  instances_req.SetMetadata(metadata);
  InstancesResponse* instances_resp = nullptr;
  for (int i = 0; i < 10; i++) {
    instances_req.SetMetadataFailover(kMetadataFailoverNone);
    ASSERT_EQ(consumer_->GetInstances(instances_req, instances_resp), kReturnInstanceNotFound);
    instances_req.SetMetadataFailover(kMetadataFailoverNotKey);
    ASSERT_EQ(consumer_->GetInstances(instances_req, instances_resp), kReturnOk);
    ASSERT_EQ(instances_resp->GetInstances().size(), 2);
    delete instances_resp;
    one_instance_request.SetMetadataFailover(kMetadataFailoverAll);
    ASSERT_EQ(consumer_->GetInstances(instances_req, instances_resp), kReturnOk);
    ASSERT_EQ(instances_resp->GetInstances().size(), 2);
    delete instances_resp;
  }
}

}  // namespace polaris