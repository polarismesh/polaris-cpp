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

#include "mock/fake_server_response.h"
#include "model/model_impl.h"
#include "polaris/plugin.h"
#include "test_utils.h"
#include "utils/string_utils.h"

namespace polaris {

class ModelTest : public ::testing::Test {
protected:
  void SetUp() {
    service_key_.namespace_ = "test_namespace";
    service_key_.name_      = "test_service";
    TestUtils::SetUpFakeTime();
  }

  void TearDown() { TestUtils::TearDownFakeTime(); }

protected:
  ServiceKey service_key_;
};

TEST_F(ModelTest, TryChooseHalfOpenInstance) {
  Service *service = new Service(service_key_, 0);
  std::set<Instance *> service_instances;
  for (int i = 0; i < 10; i++) {
    std::string instance_id = "instance_" + StringUtils::TypeToStr<int>(i);
    Instance *instance      = new Instance(instance_id, "host", 8000, 100);
    service_instances.insert(instance);
  }
  CircuitBreakerData circuit_breaker_data;
  circuit_breaker_data.version                           = 1;
  circuit_breaker_data.half_open_instances["instance_0"] = 1;
  circuit_breaker_data.half_open_instances["instance_x"] = 2;
  service->SetCircuitBreakerData(circuit_breaker_data);

  Instance *instance = NULL;
  for (int i = 1; i <= 60; i++) {
    instance       = NULL;
    ReturnCode ret = service->TryChooseHalfOpenInstance(service_instances, instance);
    if (i == 20) {
      ASSERT_EQ(ret, kReturnOk) << i;
      ASSERT_TRUE(instance != NULL);
      ASSERT_EQ(instance->GetId(), "instance_0");
    } else {
      ASSERT_EQ(ret, kReturnInstanceNotFound);
      ASSERT_TRUE(instance == NULL);
    }
    TestUtils::FakeNowIncrement(1500);
  }

  circuit_breaker_data.version                           = 2;
  circuit_breaker_data.half_open_instances["instance_0"] = 1;
  circuit_breaker_data.half_open_instances["instance_x"] = 2;
  circuit_breaker_data.half_open_instances["instance_1"] = 5;
  service->SetCircuitBreakerData(circuit_breaker_data);
  for (int i = 1; i <= 100; i++) {
    instance = NULL;
    TestUtils::FakeNowIncrement(1500);
    ReturnCode ret = service->TryChooseHalfOpenInstance(service_instances, instance);
    if (i % 20 == 0) {
      ASSERT_EQ(ret, kReturnOk) << i;
      ASSERT_TRUE(instance != NULL);
      ASSERT_EQ(instance->GetId(), "instance_1");
    } else {
      ASSERT_EQ(ret, kReturnInstanceNotFound);
    }
  }
  instance = NULL;
  TestUtils::FakeNowIncrement(10000);
  ASSERT_EQ(service->TryChooseHalfOpenInstance(service_instances, instance),
            kReturnInstanceNotFound);
  ASSERT_TRUE(instance == NULL);

  for (std::set<Instance *>::iterator it = service_instances.begin(); it != service_instances.end();
       ++it) {
    delete *it;
  }
  delete service;
}

TEST_F(ModelTest, TryChooseHalfOpenInstanceRand) {
  Service *service = new Service(service_key_, 0);
  std::set<Instance *> instances;
  Instance *instance;
  CircuitBreakerData circuit_breaker_data;
  circuit_breaker_data.version = 1;
  service->SetCircuitBreakerData(circuit_breaker_data);
  circuit_breaker_data.version = 2;
  for (int i = 0; i < 10; i++) {
    std::string instance_id = "instance_" + StringUtils::TypeToStr<int>(i);
    instance                = new Instance(instance_id, "host", 8000, 100);
    circuit_breaker_data.half_open_instances[instance_id] = 10;
    instances.insert(instance);
  }
  service->SetCircuitBreakerData(circuit_breaker_data);

  std::set<Instance *> select_instance;
  for (int i = 0; i < 200; i++) {
    TestUtils::FakeNowIncrement(50);
    ReturnCode ret_code = service->TryChooseHalfOpenInstance(instances, instance);
    if (i % 40 == 0) {
      ASSERT_EQ(ret_code, kReturnOk) << i;
      select_instance.insert(instance);
    } else {
      ASSERT_EQ(ret_code, kReturnInstanceNotFound) << i;
    }
  }
  ASSERT_GT(select_instance.size(), 1);

  for (std::set<Instance *>::iterator it = instances.begin(); it != instances.end(); ++it) {
    delete *it;
  }
  delete service;
}

TEST_F(ModelTest, TestInstanceLocalId) {
  Service service(service_key_, 1);
  v1::DiscoverResponse response;
  std::set<uint64_t> local_id_set;
  for (int i = 0; i < 100; ++i) {
    FakeServer::CreateServiceInstances(response, service_key_, 10, i);
    response.mutable_instances(2)->mutable_isolate()->set_value(true);
    ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
    service.UpdateData(service_data);
    ServiceInstances service_instances(service_data);
    std::map<std::string, Instance *> &instances = service_instances.GetInstances();
    std::map<std::string, Instance *>::iterator instance_it;
    for (instance_it = instances.begin(); instance_it != instances.end(); ++instance_it) {
      local_id_set.insert(instance_it->second->GetLocalId());
    }
    ASSERT_EQ(local_id_set.size(), (i == 0 ? 9 : 10) + i) << i;
  }
}

}  // namespace polaris
