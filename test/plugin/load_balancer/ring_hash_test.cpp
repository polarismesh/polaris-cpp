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

#include "plugin/load_balancer/ringhash/ringhash.h"

#include <gtest/gtest.h>

#include "mock/fake_server_response.h"
#include "model/model_impl.h"
#include "test_context.h"

namespace polaris {

class RingHashCstLbTest : public ::testing::TestWithParam<bool> {
  virtual void SetUp() {
    context_.reset(TestContext::CreateContext());
    ASSERT_TRUE(context_ != nullptr);
    load_balancer_.reset(new KetamaLoadBalancer());
    std::string content, err_msg;
    if (GetParam()) {
      content.append("compatibleGo: true");
    }
    std::unique_ptr<Config> config(Config::CreateFromString(content, err_msg));
    ASSERT_EQ(load_balancer_->Init(config.get(), context_.get()), kReturnOk);
    service_key_.namespace_ = "test_namespace";
    service_key_.name_ = "test_name";
    srandom(time(nullptr));
  }

  virtual void TearDown() {}

 protected:
  void CreateInstancesResponse(v1::DiscoverResponse &response, int instance_count = 2) {
    FakeServer::InstancesResponse(response, service_key_);
    for (int i = 0; i < instance_count; ++i) {
      std::string host = std::to_string(random() % 255) + "." + std::to_string(random() % 255) + "." +
                         std::to_string(random() % 255) + "." + std::to_string(random() % 255);
      int port = 8000 + i;
      int weight = 80 + random() % 40;
      v1::Instance *instance = response.add_instances();
      instance->mutable_id()->set_value("instance_" + std::to_string(i));
      instance->mutable_host()->set_value(host);
      instance->mutable_port()->set_value(port);
      instance->mutable_weight()->set_value(weight);
    }
  }

  enum HalfOpenType { NoneHalfOpen, SomeHalfOpen, AllHalfOpen };

  void CheckChooseInstance(ServiceData *service_data, HalfOpenType half_open_type = NoneHalfOpen) {
    ServiceInstances service_instances(service_data);
    for (int i = 0; i < 10000; ++i) {
      Instance *instance = nullptr;
      Instance *instance1 = nullptr;
      Criteria criteria;
      criteria.ignore_half_open_ = (random() % 2 == 0);
      criteria.hash_key_ = random();
      ASSERT_EQ(load_balancer_->ChooseInstance(&service_instances, criteria, instance), kReturnOk);
      ASSERT_EQ(load_balancer_->ChooseInstance(&service_instances, criteria, instance1), kReturnOk);
      if (half_open_type == NoneHalfOpen) {
        ASSERT_EQ(instance, instance1);  // 没有半开实例，同一个key获取的实例相同
      } else if (criteria.ignore_half_open_) {
        ASSERT_EQ(instance, instance1);  // 有半开实例，忽略半开节点时同一个key获取到的实例也相同
      }
      criteria.replicate_index_ = 1;  // 获取备份实例时，默认过滤半开节点
      ASSERT_EQ(load_balancer_->ChooseInstance(&service_instances, criteria, instance), kReturnOk);
      ASSERT_EQ(load_balancer_->ChooseInstance(&service_instances, criteria, instance1), kReturnOk);
      ASSERT_EQ(instance, instance1);
    }
    service_data->DecrementRef();
  }

 protected:
  ServiceKey service_key_;
  std::unique_ptr<KetamaLoadBalancer> load_balancer_;
  std::unique_ptr<Context> context_;
};

TEST_P(RingHashCstLbTest, TestSelectInstance) {
  v1::DiscoverResponse response;
  CreateInstancesResponse(response);
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  Service service(service_key_, 1);
  service.UpdateData(service_data);
  CheckChooseInstance(service_data);
}

TEST_P(RingHashCstLbTest, TestSelectOnlyOneInstance) {
  v1::DiscoverResponse response;
  CreateInstancesResponse(response, 1);
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  Service service(service_key_, 1);
  service.UpdateData(service_data);
  CheckChooseInstance(service_data);
}

TEST_P(RingHashCstLbTest, TestSelectWithInstancesUpdate) {
  ServiceData *service_data = nullptr;
  LocalRegistry *local_registry = context_->GetLocalRegistry();
  local_registry->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_TRUE(service_data == nullptr);
  ServiceDataNotify *notify = nullptr;
  local_registry->LoadServiceDataWithNotify(service_key_, kServiceDataInstances, service_data, notify);
  ASSERT_TRUE(notify != nullptr);
  v1::DiscoverResponse response;
  CreateInstancesResponse(response);

  service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  local_registry->UpdateServiceData(service_key_, kServiceDataInstances, service_data);
  service_data = nullptr;
  local_registry->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_TRUE(service_data != nullptr);
  CheckChooseInstance(service_data);

  // 修改实例权重
  int update_index = random() % response.instances_size();
  uint32_t pre_weight = response.mutable_instances(update_index)->weight().value();
  response.mutable_instances(update_index)->mutable_weight()->set_value(20);
  service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  local_registry->UpdateServiceData(service_key_, kServiceDataInstances, service_data);
  service_data = nullptr;
  local_registry->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_TRUE(service_data != nullptr);
  CheckChooseInstance(service_data);

  // // 改回权重
  response.mutable_instances(update_index)->mutable_weight()->set_value(pre_weight);
  service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  local_registry->UpdateServiceData(service_key_, kServiceDataInstances, service_data);
  service_data = nullptr;
  local_registry->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_TRUE(service_data != nullptr);
  CheckChooseInstance(service_data);
}

TEST_P(RingHashCstLbTest, TestSelectWithHalfOpenInstances) {
  v1::DiscoverResponse response;
  int instance_count = 5;
  CreateInstancesResponse(response, instance_count);
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  ASSERT_TRUE(service_data != nullptr);
  Service service(service_key_, 1);
  CircuitBreakerData circuit_breaker_data;
  circuit_breaker_data.version = 1;
  for (int i = 0; i < instance_count; i += 2) {
    circuit_breaker_data.half_open_instances.insert(std::make_pair("instance_" + std::to_string(i), 8));
  }
  service.SetCircuitBreakerData(circuit_breaker_data);
  service.UpdateData(service_data);
  CheckChooseInstance(service_data, SomeHalfOpen);
}

TEST_P(RingHashCstLbTest, TestSelectWithAllHalfOpenInstances) {
  v1::DiscoverResponse response;
  int instance_count = 5;
  CreateInstancesResponse(response, instance_count);
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  ASSERT_TRUE(service_data != nullptr);
  Service service(service_key_, 1);
  CircuitBreakerData circuit_breaker_data;
  circuit_breaker_data.version = 1;
  for (int i = 0; i < instance_count; ++i) {
    circuit_breaker_data.half_open_instances.insert(std::make_pair("instance_" + std::to_string(i), 8));
  }
  service.SetCircuitBreakerData(circuit_breaker_data);
  service.UpdateData(service_data);
  CheckChooseInstance(service_data, AllHalfOpen);
}

TEST_P(RingHashCstLbTest, TestSelectReplicateInstance) {
  v1::DiscoverResponse response;
  int instance_count = 5;
  CreateInstancesResponse(response, instance_count);
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  ASSERT_TRUE(service_data != nullptr);
  Service service(service_key_, 1);
  service.UpdateData(service_data);
  ServiceInstances service_instances(service_data);
  for (int i = 0; i < 1000; ++i) {
    Criteria criteria;
    criteria.hash_key_ = random();
    std::set<Instance *> instance_set;
    for (int j = 0; j < instance_count + 1; ++j) {
      Instance *instance = nullptr;
      Instance *instance1 = nullptr;
      criteria.replicate_index_ = j;
      ASSERT_EQ(load_balancer_->ChooseInstance(&service_instances, criteria, instance), kReturnOk);
      ASSERT_EQ(load_balancer_->ChooseInstance(&service_instances, criteria, instance1), kReturnOk);
      ASSERT_TRUE(instance != nullptr);
      ASSERT_EQ(instance, instance1);
      if (j < instance_count) {
        ASSERT_TRUE(instance_set.insert(instance).second);  // 验证去重
      } else {
        criteria.replicate_index_ = 0;
        instance1 = nullptr;
        ASSERT_EQ(load_balancer_->ChooseInstance(&service_instances, criteria, instance1), kReturnOk);
        ASSERT_EQ(instance, instance1);
      }
    }
  }
  service_data->DecrementRef();
}

INSTANTIATE_TEST_CASE_P(Test, RingHashCstLbTest, ::testing::Bool());

}  // namespace polaris
