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
#include "utils/scoped_ptr.h"
#include "utils/string_utils.h"

namespace polaris {

class RingHashCstLbTest : public ::testing::Test {
  virtual void SetUp() {
    context_.Set(TestContext::CreateContext());
    ASSERT_TRUE(context_.NotNull());
    load_balancer_.Set(new KetamaLoadBalancer());
    Config *config = Config::CreateEmptyConfig();
    ASSERT_EQ(load_balancer_->Init(config, context_.Get()), kReturnOk);
    delete config;
    service_key_.namespace_ = "test_namespace";
    service_key_.name_      = "test_name";
    srandom(time(NULL));
  }

  virtual void TearDown() {}

protected:
  void CreateInstancesResponse(v1::DiscoverResponse &response) {
    FakeServer::InstancesResponse(response, service_key_);
    for (int i = 0; i < 2 /*40 + random() % 20*/; ++i) {
      std::string host = StringUtils::TypeToStr(random() % 255) + "." +
                         StringUtils::TypeToStr(random() % 255) + "." +
                         StringUtils::TypeToStr(random() % 255) + "." +
                         StringUtils::TypeToStr(random() % 255);
      int port               = 8000 + i;
      int weight             = 80 + random() % 40;
      v1::Instance *instance = response.add_instances();
      instance->mutable_id()->set_value("instance_" + StringUtils::TypeToStr<int>(i));
      instance->mutable_host()->set_value(host);
      instance->mutable_port()->set_value(port);
      instance->mutable_weight()->set_value(weight);
    }
  }

  void CheckChooseInstance(ServiceData *service_data) {
    ServiceInstances service_instances(service_data);
    for (int i = 0; i < 10000; ++i) {
      Instance *instance  = NULL;
      Instance *instance1 = NULL;
      Criteria criteria;
      criteria.hash_key_ = random();
      ASSERT_EQ(load_balancer_->ChooseInstance(&service_instances, criteria, instance), kReturnOk);
      ASSERT_EQ(load_balancer_->ChooseInstance(&service_instances, criteria, instance1), kReturnOk);
      ASSERT_EQ(instance, instance1);
      criteria.replicate_index_ = 1;
      ASSERT_EQ(load_balancer_->ChooseInstance(&service_instances, criteria, instance), kReturnOk);
      ASSERT_EQ(load_balancer_->ChooseInstance(&service_instances, criteria, instance1), kReturnOk);
      ASSERT_EQ(instance, instance1);
    }
  }

protected:
  ServiceKey service_key_;
  ScopedPtr<KetamaLoadBalancer> load_balancer_;
  ScopedPtr<Context> context_;
};

TEST_F(RingHashCstLbTest, TestSelectInstance) {
  v1::DiscoverResponse response;
  CreateInstancesResponse(response);
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  Service service(service_key_, 1);
  service.UpdateData(service_data);
  CheckChooseInstance(service_data);
}

TEST_F(RingHashCstLbTest, TestSelectWithInstancesUpdate) {
  ServiceData *service_data     = NULL;
  LocalRegistry *local_registry = context_->GetLocalRegistry();
  local_registry->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_TRUE(service_data == NULL);
  v1::DiscoverResponse response;
  CreateInstancesResponse(response);

  service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  local_registry->UpdateServiceData(service_key_, kServiceDataInstances, service_data);
  service_data = NULL;
  local_registry->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_TRUE(service_data != NULL);
  CheckChooseInstance(service_data);

  // 修改实例权重
  int update_index    = random() % response.instances_size();
  uint32_t pre_weight = response.mutable_instances(update_index)->weight().value();
  response.mutable_instances(update_index)->mutable_weight()->set_value(20);
  service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  local_registry->UpdateServiceData(service_key_, kServiceDataInstances, service_data);
  service_data = NULL;
  local_registry->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_TRUE(service_data != NULL);
  CheckChooseInstance(service_data);

  // // 改回权重
  response.mutable_instances(update_index)->mutable_weight()->set_value(pre_weight);
  service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  local_registry->UpdateServiceData(service_key_, kServiceDataInstances, service_data);
  service_data = NULL;
  local_registry->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_TRUE(service_data != NULL);
  CheckChooseInstance(service_data);
}

}  // namespace polaris
