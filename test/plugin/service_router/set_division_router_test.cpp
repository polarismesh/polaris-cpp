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

#include "plugin/service_router/set_division_router.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "mock/fake_server_response.h"
#include "model/constants.h"
#include "model/model_impl.h"
#include "test_context.h"
#include "utils/utils.h"

namespace polaris {

///////////////////////////////////////////////////////////////////////////////
// Set路由测试
class SetDivisionServiceRouterTest : public ::testing::Test {
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    ASSERT_TRUE(context_ != NULL);
    service_router_ = new SetDivisionServiceRouter();
    Config *config  = Config::CreateEmptyConfig();
    ASSERT_EQ(service_router_->Init(config, context_), kReturnOk);
    delete config;

    // set callee instances
    Instance *instance1 = new Instance("1", "127.0.0.1", 10001, 100);
    InstanceSetter instance_setter1(*instance1);
    instance_setter1.AddMetadataItem(SetDivisionServiceRouter::enable_set_key, "Y");
    instance_setter1.AddMetadataItem(constants::kRouterRequestSetNameKey, "app.sz.1");
    instance_setter1.SetHealthy(true);
    callee_instances_.push_back(instance1);

    Instance *instance2 = new Instance("2", "127.0.0.1", 10002, 100);
    InstanceSetter instance_setter2(*instance2);
    instance_setter2.AddMetadataItem(SetDivisionServiceRouter::enable_set_key, "Y");
    instance_setter2.AddMetadataItem(constants::kRouterRequestSetNameKey, "app.sh.1");
    instance_setter2.SetHealthy(true);
    callee_instances_.push_back(instance2);

    Instance *instance3 = new Instance("3", "127.0.0.1", 10003, 100);
    InstanceSetter instance_setter3(*instance3);
    instance_setter3.AddMetadataItem(SetDivisionServiceRouter::enable_set_key, "N");
    instance_setter3.AddMetadataItem(constants::kRouterRequestSetNameKey, "app.sz.1");
    instance_setter3.SetHealthy(true);
    callee_instances_.push_back(instance3);

    Instance *instance4 = new Instance("4", "127.0.0.1", 10004, 100);
    InstanceSetter instance_setter4(*instance4);
    instance_setter4.AddMetadataItem(SetDivisionServiceRouter::enable_set_key, "Y");
    instance_setter4.AddMetadataItem(constants::kRouterRequestSetNameKey, "app.sz.*");
    instance_setter4.SetHealthy(true);
    callee_instances_.push_back(instance4);

    Instance *instance5 = new Instance("5", "127.0.0.1", 10005, 100);
    InstanceSetter instance_setter5(*instance5);
    instance_setter5.AddMetadataItem(SetDivisionServiceRouter::enable_set_key, "Y");
    instance_setter5.AddMetadataItem(constants::kRouterRequestSetNameKey, "app.sz.2");
    instance_setter5.SetHealthy(true);
    callee_instances_.push_back(instance5);

    Instance *instance6 = new Instance("6", "127.0.0.1", 10006, 100);
    InstanceSetter instance_setter6(*instance6);
    instance_setter6.AddMetadataItem(SetDivisionServiceRouter::enable_set_key, "Y");
    instance_setter6.AddMetadataItem(constants::kRouterRequestSetNameKey, "app.sz.1");
    instance_setter6.SetHealthy(false);
    callee_instances_.push_back(instance6);
    unhealthy_set_.insert(instance6);
  }

  virtual void TearDown() {
    for (std::size_t i = 0; i < callee_instances_.size(); ++i) {
      delete callee_instances_[i];
    }

    if (service_router_ != NULL) {
      delete service_router_;
      service_router_ = NULL;
    }
    if (context_ != NULL) {
      delete context_;
      context_ = NULL;
    }
  }

public:
  std::vector<Instance *> callee_instances_;
  std::set<Instance *> unhealthy_set_;

  SetDivisionServiceRouter *service_router_;
  Context *context_;
};

TEST_F(SetDivisionServiceRouterTest, IsSetDivisionRouterEnable) {
  std::map<std::string, std::string> callee_metadata;

  std::string caller_set_name = "app.sz.1";
  std::string callee_set_name = "app1.sz.1";

  callee_metadata.insert(std::make_pair(constants::kRouterRequestSetNameKey, "app1.sz.1"));
  callee_metadata.insert(std::make_pair(SetDivisionServiceRouter::enable_set_key, "N"));
  bool enable =
      service_router_->IsSetDivisionRouterEnable(caller_set_name, callee_set_name, callee_metadata);
  EXPECT_EQ(enable, false);

  callee_metadata.clear();
  callee_set_name = "app1.sz.1";
  callee_metadata.insert(std::make_pair(constants::kRouterRequestSetNameKey, "app1.sz.1"));
  callee_metadata.insert(std::make_pair(SetDivisionServiceRouter::enable_set_key, "Y"));
  enable =
      service_router_->IsSetDivisionRouterEnable(caller_set_name, callee_set_name, callee_metadata);
  EXPECT_EQ(enable, false);

  callee_metadata.clear();
  callee_set_name = "app.sh.1";
  callee_metadata.insert(std::make_pair(constants::kRouterRequestSetNameKey, "app.sh.1"));
  callee_metadata.insert(std::make_pair(SetDivisionServiceRouter::enable_set_key, "Y"));
  enable =
      service_router_->IsSetDivisionRouterEnable(caller_set_name, callee_set_name, callee_metadata);
  EXPECT_EQ(enable, true);

  callee_metadata.clear();
  callee_set_name = "app.sz.1";
  callee_metadata.insert(std::make_pair(constants::kRouterRequestSetNameKey, "app.sz.1"));
  callee_metadata.insert(std::make_pair(SetDivisionServiceRouter::enable_set_key, "N"));
  enable =
      service_router_->IsSetDivisionRouterEnable(caller_set_name, callee_set_name, callee_metadata);
  EXPECT_EQ(enable, false);

  callee_metadata.clear();
  callee_set_name = "app.sz.1";
  callee_metadata.insert(std::make_pair(constants::kRouterRequestSetNameKey, "app.sz.1"));
  callee_metadata.insert(std::make_pair(SetDivisionServiceRouter::enable_set_key, "Y"));
  enable =
      service_router_->IsSetDivisionRouterEnable(caller_set_name, callee_set_name, callee_metadata);
  EXPECT_EQ(enable, true);
}

TEST_F(SetDivisionServiceRouterTest, CalculateMatchResult) {
  std::vector<Instance *> result;
  // set内有节点，只返回本set内的节点
  std::string caller_set_name = "app.sz.1";
  service_router_->CalculateMatchResult(caller_set_name, callee_instances_, result);
  ASSERT_EQ(result.size(), 2);
  EXPECT_EQ(result[0]->GetId(), "1");
  EXPECT_EQ(result[1]->GetId(), "6");

  // 主调使用的通配set，返回所有app.area下的所有节点
  caller_set_name = "app.sz.*";
  result.clear();
  service_router_->CalculateMatchResult(caller_set_name, callee_instances_, result);
  EXPECT_EQ(result.size(), 4);

  // set内无指定的节点，返回(groupID为*)通配set里的节点
  caller_set_name = "app.sz.3";
  result.clear();
  service_router_->CalculateMatchResult(caller_set_name, callee_instances_, result);
  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result[0]->GetId(), "4");

  // set内没有节点，且没有通配set，返回empty
  caller_set_name = "app.tj.1";
  result.clear();
  service_router_->CalculateMatchResult(caller_set_name, callee_instances_, result);
  EXPECT_EQ(result.size(), 0);
}

TEST_F(SetDivisionServiceRouterTest, GetHealthyInstances) {
  std::vector<Instance *> result;
  std::string caller_set_name = "app.sz.1";
  service_router_->CalculateMatchResult(caller_set_name, callee_instances_, result);
  EXPECT_EQ(result.size(), 2);
  std::vector<Instance *> healthy_result;

  service_router_->GetHealthyInstances(result, unhealthy_set_, healthy_result);
  ASSERT_EQ(healthy_result.size(), 1);
  EXPECT_EQ(healthy_result[0]->GetId(), "1");
}

TEST_F(SetDivisionServiceRouterTest, DoRoute) {
  v1::DiscoverResponse response;
  ServiceKey service_key = {"Test", "test.app"};
  FakeServer::InstancesResponse(response, service_key);
  for (std::size_t i = 0; i < callee_instances_.size(); i++) {
    v1::Instance *instance = response.add_instances();
    instance->mutable_id()->set_value(callee_instances_[i]->GetId());
    instance->mutable_host()->set_value(callee_instances_[i]->GetHost());
    instance->mutable_port()->set_value(callee_instances_[i]->GetPort());
    instance->mutable_healthy()->set_value(callee_instances_[i]->isHealthy());
    instance->mutable_metadata()->insert(callee_instances_[i]->GetMetadata().begin(),
                                         callee_instances_[i]->GetMetadata().end());
    instance->mutable_weight()->set_value(callee_instances_[i]->GetWeight());
  }

  Service *service          = new Service(service_key, 0);
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  service->UpdateData(service_data);
  // service_data->IncrementRef();

  ServiceInfo *source_service_info                                    = new ServiceInfo();
  source_service_info->service_key_.namespace_                        = "Test";
  source_service_info->service_key_.name_                             = "test.client";
  source_service_info->metadata_[constants::kRouterRequestSetNameKey] = "app.sz.1";
  RouteInfo route_info(service_key, source_service_info);
  route_info.SetServiceInstances(new ServiceInstances(service_data));

  RouteResult route_result;
  ReturnCode ret = service_router_->DoRoute(route_info, &route_result);
  EXPECT_EQ(ret, 0);
  InstancesSet *instances_set = route_result.GetServiceInstances()->GetAvailableInstances();
  ASSERT_EQ(instances_set->GetInstances().size(), 1);
  EXPECT_EQ(instances_set->GetInstances()[0]->GetId(), "1");

  delete service;
}

}  // namespace polaris
