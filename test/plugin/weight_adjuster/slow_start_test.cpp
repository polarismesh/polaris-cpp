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

#include "plugin/weight_adjuster/slow_start.h"

#include <gtest/gtest.h>

#include "context/context_impl.h"
#include "context/service_context.h"
#include "mock/fake_server_response.h"
#include "test_utils.h"

namespace polaris {

class SlowStartTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    service_key_ = {"Test", "test.slow.start"};
    std::string err_msg, content = R"###(
global:
  serverConnector:
    addresses:
    - 127.0.0.1:8091      
consumer:
  service:
    - name: test.slow.start
      namespace: Test
      loadBalancer:
        type: ringHash
      weightAdjuster:
        name: slowStart
)###";
    std::unique_ptr<Config> config(Config::CreateFromString(content, err_msg));
    ASSERT_TRUE(config != nullptr && err_msg.empty());
    context_.reset(Context::Create(config.get(), kShareContextWithoutEngine));
    ASSERT_TRUE(context_ != nullptr);
    TestUtils::SetUpFakeTime();
  }

  virtual void TearDown() { TestUtils::TearDownFakeTime(); }

 protected:
  ServiceData* CreateInstances(int count) {
    v1::DiscoverResponse response;
    FakeServer::InstancesResponse(response, service_key_);
    for (int i = 0; i < count; ++i) {
      v1::Instance* instance = response.add_instances();
      instance->mutable_id()->set_value("instance_" + std::to_string(i));
      instance->mutable_host()->set_value("127.0.0.1");
      instance->mutable_port()->set_value(8000 + i);
      instance->mutable_weight()->set_value(100);
    }
    return ServiceData::CreateFromPb(&response, kDataIsSyncing);
  }

 protected:
  ServiceKey service_key_;
  std::unique_ptr<Context> context_;
};

TEST_F(SlowStartTest, SlowStartAdjuster) {
  auto service_context = context_->GetContextImpl()->GetServiceContext(service_key_);
  ASSERT_TRUE(service_context != nullptr);

  auto local_registry = context_->GetLocalRegistry();
  ServiceData* service_data = nullptr;
  ServiceDataNotify* notify = nullptr;
  auto ret_code = local_registry->LoadServiceDataWithNotify(service_key_, kServiceDataInstances, service_data, notify);
  ASSERT_EQ(ret_code, kReturnOk);
  local_registry->UpdateServiceData(service_key_, kServiceDataInstances, CreateInstances(5));

  ret_code = local_registry->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_TRUE(ret_code == kReturnOk && service_data != nullptr);

  std::unique_ptr<ServiceInstances> service_instances(new ServiceInstances(service_data));
  for (auto& instance_it : service_instances->GetInstances()) {
    ASSERT_EQ(instance_it.second->GetDynamicWeight(), 100);  // 初始加载时，没有慢启动
  }
  service_data->DecrementRef();

  // 添加新实例
  local_registry->UpdateServiceData(service_key_, kServiceDataInstances, CreateInstances(10));
  ret_code = local_registry->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_TRUE(ret_code == kReturnOk && service_data != nullptr);
  service_instances.reset(new ServiceInstances(service_data));
  for (auto& instance_it : service_instances->GetInstances()) {
    if (instance_it.second->GetPort() < 8005) {
      ASSERT_EQ(instance_it.second->GetDynamicWeight(), 100);  // 之前的实例不变
    } else {
      ASSERT_EQ(instance_it.second->GetDynamicWeight(), 10);  // 新增的实例为初始值
    }
  }
  service_data->DecrementRef();

  WeightAdjuster* weight_adjuster = service_context->GetWeightAdjuster();
  uint32_t weight_list[] = {16, 33, 50, 66, 83, 100};
  for (int i = 0; i < 6; ++i) {
    TestUtils::FakeSteadyTimeInc(10 * 1000);
    ASSERT_EQ(weight_adjuster->DoAdjust(service_data), i < 5) << i;
    for (auto& instance_it : service_instances->GetInstances()) {
      if (instance_it.second->GetPort() < 8005) {
        ASSERT_EQ(instance_it.second->GetDynamicWeight(), 100);  // 之前的实例不变
      } else {
        EXPECT_EQ(instance_it.second->GetDynamicWeight(), weight_list[i]);  // 新增的实例为初始值
      }
    }
  }
}

}  // namespace polaris
