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

#include <memory>

#include "mock/fake_server_response.h"
#include "model/model_impl.h"
#include "test_context.h"

namespace polaris {

class RuleRouterMultiEnvTest : public ::testing::Test {
  virtual void SetUp() {
    context_.reset(TestContext::CreateContext());
    ASSERT_TRUE(context_ != nullptr);
    std::unique_ptr<Config> config(Config::CreateEmptyConfig());
    ASSERT_EQ(service_router_.Init(config.get(), context_.get()), kReturnOk);
    service_key_.namespace_ = "Test";
    service_key_.name_ = "env.test.service";
    service_.reset(new Service(service_key_, 0));
    service_instances_ = nullptr;
    service_route_ = nullptr;
  }

  virtual void TearDown() {
    if (service_instances_ != nullptr) service_instances_->DecrementRef();
    if (service_route_ != nullptr) service_route_->DecrementRef();
  }

 protected:
  void InitRouterRule(const v1::MatchString &base_match,
                      v1::MatchString::MatchStringType parameter_type = v1::MatchString::EXACT);
  void InitInstances(const std::string &base_env);
  void CheckEnvRoute(const std::string &base_env);

 protected:
  std::unique_ptr<Context> context_;
  RuleServiceRouter service_router_;
  ServiceKey service_key_;
  std::unique_ptr<Service> service_;
  ServiceData *service_instances_;
  ServiceData *service_route_;
};

TEST_F(RuleRouterMultiEnvTest, MultiEvnWithRegex) {
  /* 多环境主调配置路由规则
  "routing": {
    "service": "svr1",  // 在服务svr1上配置规则
    "namespace": "Test",
    "outbounds": [
      {  "source": [  // 根据主调方传入服务名和参数匹配
            { "service": "svr1", "metadata": {"env": "base", "key": "0-99" } } ],
          "destination": [  // 根据被调方的服务实例metadata匹配
            { "service": "*", "metadata": {"env": "base"}, "priority": 0 },
            { "service": "*", "metadata": {"env": "test1"}, "priority": 1} ]},
      {   "source": [
            { "service": "svr1", "metadata": {"env": "base", "key": "100-199"} } ],
          "destination": [ { "service": "*", "metadata": {"env": "base"}, "priority": 1 },
            { "service": "*", "metadata": {"env": "test1"}, "priority": 0 } ]
      }
    ] }*/
  service_key_.name_ = "srv1";
  v1::DiscoverResponse response;
  FakeServer::RoutingResponse(response, service_key_);
  v1::MatchString exact_string, regex_string;
  exact_string.set_type(v1::MatchString::EXACT);
  regex_string.set_type(v1::MatchString::REGEX);
  v1::Routing *routing = response.mutable_routing();
  for (int i = 0; i < 2; ++i) {
    v1::Route *route = routing->add_outbounds();
    v1::Source *source = route->add_sources();
    source->mutable_namespace_()->set_value(service_key_.namespace_);
    source->mutable_service()->set_value(service_key_.name_);
    exact_string.mutable_value()->set_value("base");
    (*source->mutable_metadata())["env"] = exact_string;
    regex_string.mutable_value()->set_value(i == 0 ? "^([0-9]|[1-9][0-9])$" : "^1([0-9][0-9])$");
    (*source->mutable_metadata())["key"] = regex_string;
    for (int j = 0; j < 2; ++j) {
      v1::Destination *destination = route->add_destinations();
      destination->mutable_namespace_()->set_value("*");
      destination->mutable_service()->set_value("*");
      exact_string.mutable_value()->set_value(j == 0 ? "base" : "test1");
      (*destination->mutable_metadata())["env"] = exact_string;
      destination->mutable_priority()->set_value(i == j ? 0 : 1);
    }
  }
  ServiceData *source_route_rule = ServiceData::CreateFromPb(&response, kDataIsSyncing);

  // 目标服务路由配置为空
  response.mutable_routing()->Clear();
  service_route_ = ServiceData::CreateFromPb(&response, kDataIsSyncing);

  /* 目标服务实例
  instances: [
    {id: "instance_0", host: "service_host", port: 8000, weight: 100, metadata: {"env": "test1"}},
    {id: "instance_1", host: "service_host", port: 8001, weight: 100, metadata: {"env": "base"}},
    {id: "instance_2", host: "service_host", port: 8002, weight: 100, metadata: {"env": "base"}},
    {id: "instance_3", host: "service_host", port: 8003, weight: 100, metadata: {"env": "test1"}},
    {id: "instance_4", host: "service_host", port: 8004, weight: 100, metadata: {"env": "base"}},]*/
  response.Clear();
  FakeServer::InstancesResponse(response, service_key_);
  for (int i = 0; i < 5; ++i) {
    v1::Instance *instance = response.add_instances();
    instance->mutable_id()->set_value("instance_" + std::to_string(i));
    instance->mutable_host()->set_value("service_host");
    instance->mutable_port()->set_value(8000 + i);
    instance->mutable_weight()->set_value(100);
    (*instance->mutable_metadata())["env"] = i % 3 == 0 ? "test1" : "base";
  }
  service_instances_ = ServiceData::CreateFromPb(&response, kDataInitFromDisk);
  service_->UpdateData(service_instances_);

  for (int i = 0; i < 2; ++i) {
    ServiceInfo source_service_info;
    source_service_info.service_key_ = service_key_;
    source_service_info.metadata_["env"] = "base";
    source_service_info.metadata_["key"] = i == 0 ? "88" : "188";
    RouteInfo route_info(service_key_, &source_service_info);
    route_info.SetServiceInstances(new ServiceInstances(service_instances_));
    route_info.SetServiceRouteRule(new ServiceRouteRule(service_route_));
    route_info.SetSourceServiceRouteRule(new ServiceRouteRule(source_route_rule));
    RouteResult route_result;
    ASSERT_EQ(service_router_.DoRoute(route_info, &route_result), kReturnOk);
    ASSERT_TRUE(route_info.GetServiceInstances() != nullptr);
    InstancesSet *instances_set = route_info.GetServiceInstances()->GetAvailableInstances();
    if (i == 0) {  // 选择 base环境的实例
      ASSERT_EQ(instances_set->GetInstances().size(), 3);
    } else {  // 选择test环境的实例
      ASSERT_EQ(instances_set->GetInstances().size(), 2);
    }
  }
  source_route_rule->DecrementRef();
}

void RuleRouterMultiEnvTest::InitRouterRule(const v1::MatchString &base_match,
                                            v1::MatchString::MatchStringType parameter_type) {
  v1::DiscoverResponse response;
  FakeServer::RoutingResponse(response, service_key_);
  v1::Routing *routing = response.mutable_routing();
  v1::Route *route = routing->add_inbounds();
  v1::Source *source = route->add_sources();
  (*source->mutable_metadata())["env"].set_value_type(v1::MatchString::PARAMETER);
  (*source->mutable_metadata())["env"].set_type(parameter_type);
  v1::Destination *destination = route->add_destinations();
  (*destination->mutable_metadata())["env"].set_value_type(v1::MatchString::PARAMETER);
  (*destination->mutable_metadata())["env"].set_type(parameter_type);
  destination->mutable_priority()->set_value(0);
  destination = route->add_destinations();
  (*destination->mutable_metadata())["env"] = base_match;
  route = routing->add_inbounds();
  destination = route->add_destinations();
  (*destination->mutable_metadata())["env"] = base_match;
  service_route_ = ServiceData::CreateFromPb(&response, kDataIsSyncing);
}

void RuleRouterMultiEnvTest::InitInstances(const std::string &base_env) {
  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key_);
  for (int i = 0; i < 20; ++i) {
    v1::Instance *instance = response.add_instances();
    instance->mutable_id()->set_value("instance_" + std::to_string(i));
    instance->mutable_host()->set_value("service_host");
    instance->mutable_port()->set_value(8000 + i);
    instance->mutable_weight()->set_value(100);
    (*instance->mutable_metadata())["env"] = (i % 3 == 0 ? base_env : "test" + std::to_string(i));
  }
  service_instances_ = ServiceData::CreateFromPb(&response, kDataInitFromDisk);
  service_->UpdateData(service_instances_);
}

void RuleRouterMultiEnvTest::CheckEnvRoute(const std::string &base_env) {
  // 服务发现
  ServiceInfo source_service_info;
  RouteInfo route_info(service_key_, &source_service_info);
  route_info.SetServiceRouteRule(new ServiceRouteRule(service_route_));

  // 不传env时，兜底到base环境
  source_service_info.metadata_["set2"] = "set2";
  RouteResult route_result;
  route_info.SetServiceInstances(new ServiceInstances(service_instances_));
  ASSERT_EQ(service_router_.DoRoute(route_info, &route_result), kReturnOk);
  ServiceInstances *result_instances = route_info.GetServiceInstances();
  route_info.SetServiceInstances(nullptr);
  ASSERT_TRUE(result_instances != nullptr);
  InstancesSet *instances_set = result_instances->GetAvailableInstances();
  ASSERT_EQ(instances_set->GetInstances().size(), 7);  // 选择 base环境的实例
  delete result_instances;

  // 传env时
  for (int i = 0; i < 20; ++i) {
    source_service_info.metadata_["env"] = "test" + std::to_string(i);
    route_info.SetServiceInstances(new ServiceInstances(service_instances_));
    ASSERT_EQ(service_router_.DoRoute(route_info, &route_result), kReturnOk);
    result_instances = route_info.GetServiceInstances();
    route_info.SetServiceInstances(nullptr);
    ASSERT_TRUE(result_instances != nullptr);
    instances_set = result_instances->GetAvailableInstances();
    ASSERT_EQ(instances_set->GetInstances().size(), i % 3 == 0 ? 7 : 1);
    delete result_instances;
  }

  // 主动选择base环境
  source_service_info.metadata_["env"] = base_env;
  route_info.SetServiceInstances(new ServiceInstances(service_instances_));
  ASSERT_EQ(service_router_.DoRoute(route_info, &route_result), kReturnOk);
  result_instances = route_info.GetServiceInstances();
  ASSERT_TRUE(result_instances != nullptr);
  instances_set = result_instances->GetAvailableInstances();
  ASSERT_EQ(instances_set->GetInstances().size(), 7);  // 选择 base环境的实例
}

TEST_F(RuleRouterMultiEnvTest, MultiEvnWithParameter) {
  /* 多环境被调配置带参数的路由规则
  "routing": {
    "inbounds": [
      {  "source": [  // 根据主调方传入服务名和参数匹配
            { "metadata": {"env": {"value_type": "PARAMETER"} } } ],
          "destination": [
            { "metadata": {"env": {"value_type": "PARAMETER"} }, "priority": 0 },
            { "metadata": {"env": {"type": "EXACT", "value": "base" } }} ]},
      {   "source": [ ],
          "destination": [ { "metadata": {"env": {"type": "EXACT", "value": "base"} } ] }
    ] }*/

  std::string base_env = "base";
  v1::MatchString base_match;
  base_match.set_type(v1::MatchString::EXACT);
  base_match.mutable_value()->set_value(base_env);
  InitRouterRule(base_match);

  // 初始化实例
  InitInstances("base");

  // 服务发现
  CheckEnvRoute(base_env);
}

TEST_F(RuleRouterMultiEnvTest, MultiEvnWithParameterRegex) {
  /* 多环境被调配置带参数的路由规则
  "routing": {
    "inbounds": [
      {  "source": [  // 根据主调方传入服务名和参数匹配
            { "metadata": {"env": {"value_type": "PARAMETER", "type": "REGEX"} } } ],
          "destination": [
            { "metadata": {"env": {"value_type": "PARAMETER", "type": "REGEX"} }, "priority": 0 },
            { "metadata": {"env": {"type": "EXACT", "value": "base" } }} ]},
      {   "source": [ ],
          "destination": [ { "metadata": {"env": {"type": "EXACT", "value": "base"} } ] }
    ] }*/

  std::string base_env = "base";
  v1::MatchString base_match;
  base_match.set_type(v1::MatchString::EXACT);
  base_match.mutable_value()->set_value(base_env);
  InitRouterRule(base_match, v1::MatchString::REGEX);

  // 初始化实例
  InitInstances("base");

  // 服务发现
  ServiceInfo source_service_info;
  RouteInfo route_info(service_key_, &source_service_info);
  route_info.SetServiceRouteRule(new ServiceRouteRule(service_route_));
  source_service_info.metadata_["env"] = "test.*";
  route_info.SetServiceInstances(new ServiceInstances(service_instances_));
  RouteResult route_result;
  ASSERT_EQ(service_router_.DoRoute(route_info, &route_result), kReturnOk);
  ServiceInstances *result_instances = route_info.GetServiceInstances();
  ASSERT_TRUE(result_instances != nullptr);
  InstancesSet *instances_set = result_instances->GetAvailableInstances();
  ASSERT_EQ(instances_set->GetInstances().size(), 13);
}

TEST_F(RuleRouterMultiEnvTest, MultiEvnWithVariable) {
  /* 多环境被调配置带变量的路由规则
  "routing": {
    "inbounds": [
      {  "source": [  // 根据主调方传入服务名和参数匹配
            { "metadata": {"env": {"value_type": "PARAMETER"} } } ],
          "destination": [
            { "metadata": {"env": {"value_type": "PARAMETER"} }, "priority": 0 },
            { "metadata": {"env": {"value_type": "VARIABLE", "value": "base_env" } }} ]},
      {   "source": [ ],
          "destination": [ { "metadata": {"env": {"value_type": "VARIABLE", "value": "base_env"} } ]
  } ] }*/

  v1::MatchString base_match;
  base_match.set_value_type(v1::MatchString::VARIABLE);
  base_match.mutable_value()->set_value("base_env");
  InitRouterRule(base_match);

  std::string base_env = "base123";
  InitInstances(base_env);

  // 设置环境变量
  setenv("base_env", base_env.c_str(), 1);
  SystemVariables system_variables;
  service_route_->GetServiceDataImpl()->FillSystemVariables(system_variables);
  CheckEnvRoute(base_env);
}

TEST_F(RuleRouterMultiEnvTest, MultiEvnWithVariableRegex) {
  /* 多环境被调配置带变量的路由规则
  "routing": {
    "inbounds": [
      {  "source": [  // 根据主调方传入服务名和参数匹配
            { "metadata": {"env": {"value_type": "PARAMETER"} } } ],
          "destination": [
            { "metadata": {"env": {"value_type": "PARAMETER"} }, "priority": 0 },
            { "metadata": {"env": {"value_type": "VARIABLE", "type": "REGEX", "value": "base.*" } }}
      ]},
      {   "source": [ ],
          "destination": [ { "metadata": {"env": {"value_type": "type": "REGEX", "value": "base.*" }
           } ]  } ] }*/

  v1::MatchString base_match;

  base_match.set_value_type(v1::MatchString::VARIABLE);
  base_match.set_type(v1::MatchString::REGEX);
  base_match.mutable_value()->set_value("base_env");
  InitRouterRule(base_match);

  std::string base_env = "baseABC";
  InitInstances(base_env);

  // 设置环境变量
  setenv("base_env", "base.*", 1);
  SystemVariables system_variables;
  service_route_->GetServiceDataImpl()->FillSystemVariables(system_variables);
  CheckEnvRoute(base_env);
}

}  // namespace polaris
