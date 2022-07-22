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

#include "polaris/consumer.h"
#include "utils/time_clock.h"

#include "integration/common/environment.h"
#include "integration/common/integration_base.h"

namespace polaris {

class RingHashWithSlowStartTest : public IntegrationBase {
 protected:
  RingHashWithSlowStartTest() : consumer_api_(nullptr) {}

  virtual void SetUp() {
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("cpp.integration.slow.start" + std::to_string(Time::GetSystemTimeMs()));
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
        "    - name: " +
        service_.name().value() +
        "\n      namespace: Test\n"
        "      loadBalancer:\n"
        "        type: ringHash\n"
        "      weightAdjuster:\n"
        "        name: slowStart\n";
    std::string err_msg;
    std::unique_ptr<Config> config(Config::CreateFromString(config_string_, err_msg));
    ASSERT_TRUE(config != nullptr) << config_string_ << err_msg;
    context_ = Context::Create(config.get(), kShareContext);
    ASSERT_TRUE(context_ != nullptr);
    IntegrationBase::SetUp();
    consumer_api_ = ConsumerApi::Create(context_);
    ASSERT_TRUE(consumer_api_ != nullptr);
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

  void CreateInstances(std::size_t instance_num);

  void DeleteInstances();

 protected:
  ConsumerApi* consumer_api_;
  std::vector<std::string> instances_;
};

void RingHashWithSlowStartTest::CreateInstances(std::size_t instance_num) {
  std::string instance_id;
  for (std::size_t i = instances_.size(); i < instance_num; ++i) {
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

void RingHashWithSlowStartTest::DeleteInstances() {
  for (std::size_t i = 0; i < instances_.size(); ++i) {
    IntegrationBase::DeletePolarisServiceInstance(service_token_, instances_[i]);
  }
}

TEST_F(RingHashWithSlowStartTest, SlowStartTest) {
  ServiceKey service_key;
  service_key.namespace_ = service_.namespace_().value();
  service_key.name_ = service_.name().value();
  GetOneInstanceRequest request(service_key);
  Instance instance;
  std::set<uint32_t> port_set;
  for (int i = 0; i < 10000; ++i) {
    request.SetHashKey(random());
    EXPECT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    port_set.insert(instance.GetPort());
    if (port_set.size() == instances_.size()) {
      break;  // 实例都获取到了
    }
  }
  ASSERT_EQ(port_set.size(), instances_.size());  // 实例都获取到了

  CreateInstances(2);  // 加2个实例
  sleep(5);
  port_set.clear();
  for (int i = 0; i < 10000; ++i) {
    request.SetHashKey(random());
    EXPECT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    port_set.insert(instance.GetPort());
    if (port_set.size() == instances_.size()) {
      break;  // 实例都获取到了
    }
  }
  ASSERT_EQ(port_set.size(), instances_.size());

  uint32_t weight_list[] = {10, 16, 33, 50, 66, 83, 100};
  for (int i = 0; i < 7; ++i) {
    int slow_start_count = 0;
    int total_count = 20000;
    port_set.clear();
    for (int i = 0; i < total_count; ++i) {
      request.SetHashKey(random());
      EXPECT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
      if (instance.GetPort() == 8002 || instance.GetPort() == 8003) {
        slow_start_count++;
      }
      port_set.insert(instance.GetPort());
    }
    ASSERT_EQ(port_set.size(), instances_.size());
    double slow_start_rate = static_cast<double>(slow_start_count) / total_count;
    double weight_rate = static_cast<double>(2 * weight_list[i]) / (200 + 2 * weight_list[i]);
    ASSERT_LT(slow_start_rate, weight_rate + 0.2);  // 误差不超过0.2
    sleep(10);
  }
}

}  // namespace polaris
