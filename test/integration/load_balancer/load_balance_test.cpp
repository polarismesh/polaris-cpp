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

#include "integration/common/integration_base.h"

namespace polaris {

class LoadBalanceTest : public IntegrationBase {
 protected:
  LoadBalanceTest() : consumer_api_(nullptr) {}

  virtual void SetUp() {
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("cpp.integration.load.balance.type" + std::to_string(Time::GetSystemTimeMs()));
    IntegrationBase::SetUp();
    consumer_api_ = ConsumerApi::Create(context_);
    ASSERT_TRUE(consumer_api_ != nullptr);
    srand(time(nullptr));
    CreateInstances(15 + random() % 5);
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

  void CreateInstances(int instance_num);

  void DeleteInstances();

 protected:
  ConsumerApi* consumer_api_;
  std::vector<std::string> instances_;
};

void LoadBalanceTest::CreateInstances(int instance_num) {
  std::string instance_id;
  for (int i = 0; i < instance_num; ++i) {
    v1::Instance instance;
    instance.mutable_service_token()->set_value(service_token_);
    instance.mutable_service()->set_value(service_.name().value());
    instance.mutable_namespace_()->set_value(service_.namespace_().value());
    instance.mutable_host()->set_value("127.0.0.0.1");
    instance.mutable_port()->set_value(8000 + i);
    IntegrationBase::AddPolarisServiceInstance(instance, instance_id);
    instances_.push_back(instance_id);
  }
}

void LoadBalanceTest::DeleteInstances() {
  for (std::size_t i = 0; i < instances_.size(); ++i) {
    IntegrationBase::DeletePolarisServiceInstance(service_token_, instances_[i]);
  }
}

struct LoadBalanceTestArg {
  ConsumerApi* consumer_api;
  pthread_t tid;
  ServiceKey* service_key;
};

void* LoadBalanceTypeFunc(void* arg) {
  std::map<int, std::string> key_instance_map;
  LoadBalanceTestArg* func_arg = static_cast<LoadBalanceTestArg*>(arg);
  GetOneInstanceRequest request(*func_arg->service_key);
  request.SetLoadBalanceType(kLoadBalanceTypeRingHash);
  Instance instance;
  ReturnCode ret_code;
  for (int i = 0; i < 3000; ++i) {
    int hash_key = 10 + i % 50;
    request.SetHashKey(hash_key);
    ret_code = func_arg->consumer_api->GetOneInstance(request, instance);
    EXPECT_EQ(ret_code, kReturnOk);
    std::map<int, std::string>::iterator key_it = key_instance_map.find(hash_key);
    if (key_it != key_instance_map.end()) {
      EXPECT_EQ(key_it->second, instance.GetId());
    } else {
      key_instance_map[hash_key] = instance.GetId();
    }
  }
  return nullptr;
}

TEST_F(LoadBalanceTest, CheckSetLoadBalanceType) {
  int thread_size = 4;
  ServiceKey service_key;
  service_key.namespace_ = service_.namespace_().value();
  service_key.name_ = service_.name().value();
  LoadBalanceTestArg thread_arg[thread_size];
  for (int i = 0; i < thread_size; ++i) {
    thread_arg[i].consumer_api = consumer_api_;
    thread_arg[i].service_key = &service_key;
    int rc = pthread_create(&thread_arg[i].tid, nullptr, LoadBalanceTypeFunc, &thread_arg[i]);
    ASSERT_EQ(rc, 0);
  }
  GetOneInstanceRequest request(service_key);
  Instance instance;
  for (int i = 0; i < 3000; ++i) {
    EXPECT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
  }
  for (int i = 0; i < thread_size; ++i) {
    int rc = pthread_join(thread_arg[i].tid, nullptr);
    ASSERT_EQ(rc, 0);
  }
}

TEST_F(LoadBalanceTest, CheckSimpleHash) {
  ServiceKey service_key;
  service_key.namespace_ = service_.namespace_().value();
  service_key.name_ = service_.name().value();
  GetOneInstanceRequest request(service_key);
  request.SetLoadBalanceType(kLoadBalanceTypeSimpleHash);
  Instance instance;
  std::vector<std::string> instance_order;
  std::size_t instances_size = instances_.size();
  for (std::size_t i = 0; i < instances_size; ++i) {
    request.SetHashKey(i);
    EXPECT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    instance_order.push_back(instance.GetId());
  }
  for (std::size_t i = instances_size; i < 3 * instances_size; ++i) {
    request.SetHashKey(i);
    EXPECT_EQ(consumer_api_->GetOneInstance(request, instance), kReturnOk);
    EXPECT_EQ(instance_order[i % instances_size], instance.GetId()) << i << " " << instances_size;
  }
}

}  // namespace polaris
