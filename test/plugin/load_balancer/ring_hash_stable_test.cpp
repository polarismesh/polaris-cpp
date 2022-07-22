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

// ring hash迭代稳定性验证，避免修改以后无法兼容以前的版本
class RingHashStableTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    context_.reset(TestContext::CreateContext());
    ASSERT_TRUE(context_ != nullptr);
    load_balancer_.reset(new KetamaLoadBalancer());
    Config *config = Config::CreateEmptyConfig();
    ASSERT_EQ(load_balancer_->Init(config, context_.get()), kReturnOk);
    delete config;
    service_key_.namespace_ = "test_namespace";
    service_key_.name_ = "test_name";
  }

  virtual void TearDown() {}

  void CreateInstancesResponse(v1::DiscoverResponse &response) {
    FakeServer::InstancesResponse(response, service_key_);
    for (int i = 0; i < 5; ++i) {
      v1::Instance *instance = response.add_instances();
      instance->mutable_id()->set_value("instance_" + std::to_string(i));
      instance->mutable_host()->set_value("127.0.0.1");
      instance->mutable_port()->set_value(8000 + i);
      instance->mutable_weight()->set_value(50 + i * 10);
    }
  }

 protected:
  ServiceKey service_key_;
  std::unique_ptr<KetamaLoadBalancer> load_balancer_;
  std::unique_ptr<Context> context_;
};

static std::map<std::string, std::set<int>> result = {
    {"instance_0", {1, 2, 3, 13, 21, 35, 39, 52, 58, 62, 64, 65, 66, 79, 84, 85, 90}},
    {"instance_1", {14, 18, 19, 26, 28, 29, 30, 41, 43, 71, 72, 78, 96, 97}},
    {"instance_2", {7, 8, 9, 10, 12, 17, 25, 27, 32, 33, 38, 57, 73, 74, 83, 92, 98, 99}},
    {"instance_3", {5, 6, 16, 24, 34, 40, 47, 53, 55, 59, 61, 63, 67, 75, 86, 87, 93, 94, 95}},
    {"instance_4", {4,  11, 15, 20, 22, 23, 31, 36, 37, 42, 44, 45, 46, 48, 49, 50,
                    51, 54, 56, 60, 68, 69, 70, 76, 77, 80, 81, 82, 88, 89, 91}}};

TEST_F(RingHashStableTest, TestSelectInstance) {
  v1::DiscoverResponse response;
  CreateInstancesResponse(response);
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  Service service(service_key_, 1);
  service.UpdateData(service_data);
  ServiceInstances service_instances(service_data);

  for (int i = 1; i < 100; ++i) {
    Instance *instance = nullptr;
    Criteria criteria;
    criteria.hash_key_ = i;
    ASSERT_EQ(load_balancer_->ChooseInstance(&service_instances, criteria, instance), kReturnOk);
    ASSERT_TRUE(result[instance->GetId()].count(i) > 0);
  }
  service_data->DecrementRef();
}

}  // namespace polaris
