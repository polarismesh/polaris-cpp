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

#include "plugin/service_router/rule_router.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "mock/fake_server_response.h"
#include "model/instance.h"
#include "model/model_impl.h"
#include "test_context.h"
#include "utils/utils.h"

namespace polaris {

///////////////////////////////////////////////////////////////////////////////
// 规则路由测试
class RuleServiceRouterTest : public ::testing::Test {
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    ASSERT_TRUE(context_ != nullptr);
    service_router_ = new RuleServiceRouter();
    Config *config = Config::CreateEmptyConfig();
    ASSERT_EQ(service_router_->Init(config, context_), kReturnOk);
    delete config;
  }

  virtual void TearDown() {
    if (service_router_ != nullptr) {
      delete service_router_;
      service_router_ = nullptr;
    }
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
  }

 protected:
  RuleServiceRouter *service_router_;
  Context *context_;
};

static const uint32_t kRuleDefaultPriority = 9;
static const uint32_t kRuleDefaultWeight = 0;

TEST_F(RuleServiceRouterTest, CalculateByRoute) {
  std::vector<Instance *> instances;
  std::set<Instance *> unhealthy_instances;
  for (int i = 0; i < 10; i++) {
    v1::Instance instance_pb;
    instance_pb.mutable_id()->set_value("instance_" + std::to_string(i));
    instance_pb.mutable_host()->set_value("host");
    instance_pb.mutable_port()->set_value(8000);
    instance_pb.mutable_weight()->set_value(100);
    (*instance_pb.mutable_metadata())["key2"] = "v" + std::to_string(i % 2);
    (*instance_pb.mutable_metadata())["key4"] = "v" + std::to_string(i % 4);
    Instance *instance = new Instance();
    instance->GetImpl().InitFromPb(instance_pb);
    instances.push_back(instance);
  }
  // id   0   1   2   3   4   5   6   7   8   9
  // key2 v0  v1  v0  v1  v0  v1  v0  v1  v0  v1
  // key4 v0  v1  v2  v3  v0  v1  v2  v3  v0  v1

  v1::Route route;
  v1::Destination *dest = route.add_destinations();
  v1::MatchString match_string;
  match_string.set_type(v1::MatchString::EXACT);
  RuleRouterCluster *rule_router_cluster = new RuleRouterCluster();

  // 匹配服务
  dest->mutable_namespace_()->set_value("service_namespace");
  dest->mutable_service()->set_value("service_name");
  match_string.mutable_value()->set_value("v0");
  (*dest->mutable_metadata())["key2"] = match_string;
  std::map<std::string, std::string> parameters;

  RouteRule route_rule;
  ASSERT_TRUE(route_rule.InitFromPb(route));
  ServiceKey service_key = {"other_service_namespace", "other_service_name"};
  ASSERT_TRUE(
      rule_router_cluster->CalculateByRoute(route_rule, service_key, true, instances, unhealthy_instances, parameters));
  ASSERT_TRUE(rule_router_cluster->data_.empty());

  service_key.namespace_ = "service_namespace";
  service_key.name_ = "other_service_name";
  ASSERT_TRUE(
      rule_router_cluster->CalculateByRoute(route_rule, service_key, true, instances, unhealthy_instances, parameters));
  ASSERT_TRUE(rule_router_cluster->data_.empty());

  service_key.namespace_ = "other_service_namespace";
  service_key.name_ = "service_name";
  ASSERT_TRUE(
      rule_router_cluster->CalculateByRoute(route_rule, service_key, true, instances, unhealthy_instances, parameters));
  ASSERT_TRUE(rule_router_cluster->data_.empty());

  service_key.namespace_ = "service_namespace";
  service_key.name_ = "service_name";
  ASSERT_TRUE(
      rule_router_cluster->CalculateByRoute(route_rule, service_key, true, instances, unhealthy_instances, parameters));
  ASSERT_EQ(rule_router_cluster->data_.size(), 1);
  ASSERT_TRUE(rule_router_cluster->data_.find(kRuleDefaultPriority) != rule_router_cluster->data_.end());

  delete rule_router_cluster;
  rule_router_cluster = new RuleRouterCluster();
  route.clear_destinations();
  // 匹配分组
  // 规则1：key2 == v0, priority: 1, weight: default，五个实例[0, 2, 4, 6, 8]
  dest = route.add_destinations();
  match_string.mutable_value()->set_value("v0");
  (*dest->mutable_metadata())["key2"] = match_string;
  dest->mutable_priority()->set_value(1);

  // 规则2：key4 == v2, priority: 1, weight: 100，两个实例 [2, 6]
  dest = route.add_destinations();
  match_string.mutable_value()->set_value("v2");
  (*dest->mutable_metadata())["key4"] = match_string;
  dest->mutable_priority()->set_value(1);
  dest->mutable_weight()->set_value(100);

  // 规则3：key2 == v1 && key4 = v1, priority: default, weight: 100，
  // 三个实例[1, 5, 9]
  dest = route.add_destinations();
  match_string.mutable_value()->set_value("v1");
  (*dest->mutable_metadata())["key2"] = match_string;
  (*dest->mutable_metadata())["key4"] = match_string;
  dest->mutable_weight()->set_value(100);

  // 规则4：key2 == v1 && key4 = v2, priority: 5, weight: default，匹配不到分组
  dest = route.add_destinations();
  match_string.mutable_value()->set_value("v1");
  (*dest->mutable_metadata())["key2"] = match_string;
  match_string.mutable_value()->set_value("v2");
  (*dest->mutable_metadata())["key4"] = match_string;
  dest->mutable_priority()->set_value(5);

  service_key.namespace_ = "service_namespace";
  service_key.name_ = "other_service_name";
  RouteRule route_rule2;
  ASSERT_TRUE(route_rule2.InitFromPb(route));
  ASSERT_TRUE(rule_router_cluster->CalculateByRoute(route_rule2, service_key, false, instances, unhealthy_instances,
                                                    parameters));
  ASSERT_EQ(rule_router_cluster->data_.size(), 2);  // 两个优先级
  std::map<uint32_t, std::vector<RuleRouterSet *> >::iterator priority_one = rule_router_cluster->data_.find(1);
  std::map<uint32_t, std::vector<RuleRouterSet *> >::iterator priority_default =
      rule_router_cluster->data_.find(kRuleDefaultPriority);
  ASSERT_TRUE(priority_one != rule_router_cluster->data_.end());
  ASSERT_TRUE(priority_default != rule_router_cluster->data_.end());

  // 优先级1 两个分组
  ASSERT_EQ(priority_one->second.size(), 2);
  ASSERT_EQ(priority_one->second[0]->healthy_.size(), 5);
  ASSERT_EQ(priority_one->second[0]->weight_, kRuleDefaultWeight);
  std::set<std::string> expect_set;
  expect_set.insert("instance_0");
  expect_set.insert("instance_2");
  expect_set.insert("instance_4");
  expect_set.insert("instance_6");
  expect_set.insert("instance_8");
  for (std::size_t i = 0; i < priority_one->second[0]->healthy_.size(); ++i) {
    ASSERT_TRUE(expect_set.find(priority_one->second[0]->healthy_[i]->GetId()) != expect_set.end());
  }

  ASSERT_EQ(priority_one->second[1]->healthy_.size(), 2);
  ASSERT_EQ(priority_one->second[1]->weight_, 100);
  expect_set.clear();
  expect_set.insert("instance_2");
  expect_set.insert("instance_6");
  for (std::size_t i = 0; i < priority_one->second[1]->healthy_.size(); ++i) {
    ASSERT_TRUE(expect_set.find(priority_one->second[1]->healthy_[i]->GetId()) != expect_set.end());
  }

  // 优先级default 一个分组
  ASSERT_EQ(priority_default->second.size(), 1);
  ASSERT_EQ(priority_default->second[0]->healthy_.size(), 3);
  ASSERT_EQ(priority_default->second[0]->weight_, 100);
  expect_set.clear();
  expect_set.insert("instance_1");
  expect_set.insert("instance_5");
  expect_set.insert("instance_9");
  for (std::size_t i = 0; i < priority_default->second[0]->healthy_.size(); ++i) {
    ASSERT_TRUE(expect_set.find(priority_default->second[0]->healthy_[i]->GetId()) != expect_set.end());
  }

  // 释放数据
  delete rule_router_cluster;
  for (std::vector<Instance *>::iterator it = instances.begin(); it != instances.end(); ++it) {
    delete *it;
  }
}

TEST_F(RuleServiceRouterTest, CalculateRouteResult) {
  Instance *base_id = 0;
  RuleRouterCluster rule_router_cluster;
  std::vector<RuleRouterSet *> result;
  uint32_t sum_weight = 0;
  ASSERT_FALSE(rule_router_cluster.CalculateRouteResult(result, &sum_weight, 0.3, true));
  ASSERT_TRUE(result.empty());

  RuleRouterSet *set_data = new RuleRouterSet();
  set_data->weight_ = 10;
  set_data->healthy_.push_back(base_id++);
  set_data->healthy_.push_back(base_id++);
  set_data->unhealthy_.push_back(base_id++);
  rule_router_cluster.data_[1].push_back(set_data);
  set_data = new RuleRouterSet();
  set_data->weight_ = 20;
  set_data->healthy_.push_back(base_id++);
  set_data->unhealthy_.push_back(base_id++);
  set_data->unhealthy_.push_back(base_id++);
  rule_router_cluster.data_[1].push_back(set_data);
  set_data = new RuleRouterSet();
  set_data->weight_ = 30;
  set_data->unhealthy_.push_back(base_id++);
  set_data->unhealthy_.push_back(base_id++);
  set_data->unhealthy_.push_back(base_id++);
  rule_router_cluster.data_[1].push_back(set_data);

  result.clear();
  ASSERT_FALSE(rule_router_cluster.CalculateRouteResult(result, &sum_weight, 0.3, true));
  ASSERT_EQ(result.size(), 2);
  ASSERT_EQ(result[0]->weight_, 10);
  ASSERT_EQ(result[1]->weight_, 20);

  result.clear();
  ASSERT_FALSE(rule_router_cluster.CalculateRouteResult(result, &sum_weight, 0.6, true));
  ASSERT_EQ(result.size(), 1);
  ASSERT_EQ(result[0]->weight_, 10);

  result.clear();
  // 都不满足条件，降级
  ASSERT_TRUE(rule_router_cluster.CalculateRouteResult(result, &sum_weight, 0.99, true));
  ASSERT_EQ(result.size(), 3);
  ASSERT_EQ(result[0]->healthy_.size(), 3);
  ASSERT_EQ(result[1]->healthy_.size(), 3);
  ASSERT_EQ(result[2]->healthy_.size(), 3);
}

TEST_F(RuleServiceRouterTest, TestNoRuleRoute) {
  ServiceKey service_key = {"test_namespace", "test_name"};
  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key);
  for (int i = 0; i < 5; ++i) {
    v1::Instance *instance = response.add_instances();
    instance->mutable_id()->set_value("instance_" + std::to_string(i));
    instance->mutable_host()->set_value("host");
    instance->mutable_port()->set_value(8000 + i);
    instance->mutable_weight()->set_value(100);
  }
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataInitFromDisk);
  response.mutable_routing()->Clear();
  FakeServer::RoutingResponse(response, service_key);
  ServiceData *service_route = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  Service *service = new Service(service_key, 0);
  do {
    service->UpdateData(service_data);
    RouteInfo route_info(service_key, nullptr);
    route_info.SetServiceInstances(new ServiceInstances(service_data));
    route_info.SetServiceRouteRule(new ServiceRouteRule(service_route));
    RouteResult route_result;
    ASSERT_EQ(service_router_->DoRoute(route_info, &route_result), kReturnOk);
    ServiceInstances *service_instances = route_info.GetServiceInstances();
    ASSERT_TRUE(service_instances != nullptr);
    std::map<std::string, Instance *> instances = service_instances->GetInstances();
    ASSERT_EQ(instances.size(), 5);
    for (int i = 0; i < 5; ++i) {
      ASSERT_TRUE(instances.find("instance_" + std::to_string(i)) != instances.end());
    }
  } while (false);
  delete service;
  ASSERT_EQ(service_data->DecrementAndGetRef(), 0);
  ASSERT_EQ(service_route->DecrementAndGetRef(), 0);
}

}  // namespace polaris
