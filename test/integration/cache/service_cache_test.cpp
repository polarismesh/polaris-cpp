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

#include <chrono>
#include <thread>

#include "polaris/consumer.h"
#include "utils/time_clock.h"

#include "integration/common/environment.h"
#include "integration/common/integration_base.h"

namespace polaris {

class ServiceCacheTest : public IntegrationBase {
 protected:
  ServiceCacheTest() : consumer_api_(nullptr) {}

  virtual void SetUp() {
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("cpp.integration.load.balance.type" + std::to_string(Time::GetSystemTimeMs()));
    config_string_ =
        "global:\n"
        "  serverConnector:\n"
        "    addresses: [" +
        Environment::GetDiscoverServer() +
        "]\nconsumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        Environment::GetPersistDir() +
        "\n  service:\n"
        "    - namespace: Test\n"
        "      loadBalancer:\n"
        "        type: ringHash\n"
        "        vnodeCount: 102400\n"
        "      name: " +
        service_.name().value();

    IntegrationBase::SetUp();
    consumer_api_ = ConsumerApi::CreateFromString(config_string_);
    ASSERT_TRUE(consumer_api_ != nullptr);
    CreateInstances(5);
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

  void CreateInstances(int instance_num, std::size_t start_port = 0);

  void DeleteInstances();

 protected:
  ConsumerApi* consumer_api_;
  std::vector<std::string> instances_;
};

void ServiceCacheTest::CreateInstances(int instance_num, std::size_t start_port) {
  std::string instance_id;
  for (int i = 0; i < instance_num; ++i) {
    v1::Instance instance;
    instance.mutable_service_token()->set_value(service_token_);
    instance.mutable_service()->set_value(service_.name().value());
    instance.mutable_namespace_()->set_value(service_.namespace_().value());
    instance.mutable_host()->set_value("127.0.0.0.1");
    instance.mutable_port()->set_value(8000 + start_port + i);
    IntegrationBase::AddPolarisServiceInstance(instance, instance_id);
    instances_.push_back(instance_id);
  }
}

void ServiceCacheTest::DeleteInstances() {
  for (std::size_t i = 0; i < instances_.size(); ++i) {
    IntegrationBase::DeletePolarisServiceInstance(service_token_, instances_[i]);
  }
}

TEST_F(ServiceCacheTest, CheckCacheBuildAsync) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  GetOneInstanceRequest request(service_key);
  Instance instance;
  EXPECT_EQ(consumer_api_->InitService(request), kReturnOk);
  EXPECT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);  // 首次耗时约400ms

  // 2s后新增一个节点
  std::thread add_thread([=] { CreateInstances(1, instances_.size()); });

  for (int i = 0; i < 30000; ++i) {
    request.SetHashKey(i);
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    EXPECT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    int64_t delay = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
    ASSERT_LE(delay, 10 * 1000);

    polaris::ServiceCallResult result;
    result.SetServiceNamespace(service_key.namespace_);
    result.SetServiceName(service_key.name_);
    result.SetInstanceId(instance.GetId());
    result.SetDelay(1000);
    result.SetRetCode(100);
    result.SetRetStatus(instance.GetPort() == 8000 ? polaris::kCallRetError : polaris::kCallRetOk);
    ASSERT_EQ(consumer_api_->UpdateServiceCallResult(result), kReturnOk);
    usleep(100);
  }
  add_thread.join();
}

}  // namespace polaris
