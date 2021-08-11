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

#include "model/route_rule.h"

#include <gtest/gtest.h>
#include <stdlib.h>

namespace polaris {

class RouteRuleTest : public ::testing::Test {
  virtual void SetUp() {
    source_service_info_.service_key_.namespace_ = "test_namespace";
    source_service_info_.service_key_.name_      = "test_service";
  }

  virtual void TearDown() {}

protected:
  v1::Route route_;
  RouteRule route_rule_;
  ServiceInfo source_service_info_;
  std::string parameters_;
};

TEST_F(RouteRuleTest, EmptySourceMatch) {
  route_rule_.InitFromPb(route_);
  // 空源服务匹配成功
  ASSERT_TRUE(route_rule_.MatchSource(NULL, parameters_));
}

TEST_F(RouteRuleTest, ServiceInfoSourceMatch) {
  v1::Source *source = route_.add_sources();
  source->mutable_namespace_()->set_value("test_namespace");
  source->mutable_service()->set_value("test_service");
  // 空源服务匹配失败
  route_rule_.InitFromPb(route_);
  ASSERT_FALSE(route_rule_.MatchSource(NULL, parameters_));
  // 命名空间和服务名匹配成功
  ASSERT_TRUE(route_rule_.MatchSource(&source_service_info_, parameters_));

  source_service_info_.service_key_.namespace_ = "other_test_namespace";
  source_service_info_.service_key_.name_      = "test_service";
  // 命名空间匹配失败
  ASSERT_FALSE(route_rule_.MatchSource(&source_service_info_, parameters_));

  source_service_info_.service_key_.namespace_ = "test_namespace";
  source_service_info_.service_key_.name_      = "other_test_service";
  // 服务名匹配失败
  ASSERT_FALSE(route_rule_.MatchSource(&source_service_info_, parameters_));
}

TEST_F(RouteRuleTest, ServiceInfoSourceMatchRegex) {
  v1::Source *source = route_.add_sources();
  source->mutable_namespace_()->set_value("test_namespace");
  source->mutable_service()->set_value("test_service");
  source = route_.add_sources();
  source->mutable_namespace_()->set_value("*");
  source->mutable_service()->set_value("test_service");
  source_service_info_.service_key_.namespace_ = "other_test_namespace";
  route_rule_.InitFromPb(route_);
  // 通配命名空间匹配成功
  ASSERT_TRUE(route_rule_.MatchSource(&source_service_info_, parameters_));
  ASSERT_FALSE(route_rule_.MatchSource(NULL, parameters_));

  source = route_.add_sources();
  source->mutable_namespace_()->set_value("test_namespace");
  source->mutable_service()->set_value("*");
  source_service_info_.service_key_.namespace_ = "test_namespace";
  source_service_info_.service_key_.name_      = "other_test_service";
  RouteRule route_rule1;
  route_rule1.InitFromPb(route_);
  // 通配服务名匹配成功
  ASSERT_TRUE(route_rule1.MatchSource(&source_service_info_, parameters_));
  ASSERT_FALSE(route_rule1.MatchSource(NULL, parameters_));

  source = route_.add_sources();
  source->mutable_namespace_()->set_value("*");
  source->mutable_service()->set_value("*");
  source_service_info_.service_key_.namespace_ = "other_test_namespace";
  source_service_info_.service_key_.name_      = "other_test_service";
  RouteRule route_rule2;
  route_rule2.InitFromPb(route_);
  // 通配命名空间和服务名匹配成功
  ASSERT_TRUE(route_rule2.MatchSource(&source_service_info_, parameters_));
  ASSERT_TRUE(route_rule2.MatchSource(NULL, parameters_));
}

TEST_F(RouteRuleTest, ServiceInfoSourceMatchMetadata) {  // 加上Metadata
  v1::Source *source = route_.add_sources();
  source->mutable_namespace_()->set_value("test_namespace");
  source->mutable_service()->set_value("test_service");
  v1::MatchString match_string;
  match_string.set_type(v1::MatchString::EXACT);
  match_string.mutable_value()->set_value("value");
  (*source->mutable_metadata())["key"]        = match_string;
  source_service_info_.metadata_["other_key"] = "other_value";
  route_rule_.InitFromPb(route_);
  ASSERT_FALSE(route_rule_.MatchSource(&source_service_info_, parameters_));
  source_service_info_.metadata_["key"] = "value";
  ASSERT_TRUE(route_rule_.MatchSource(&source_service_info_, parameters_));
}

TEST_F(RouteRuleTest, SourceMatchMetadataVariableMatch) {
  v1::Source *source = route_.add_sources();
  source->mutable_namespace_()->set_value("*");
  source->mutable_service()->set_value("*");
  v1::MatchString match_string;
  match_string.set_value_type(v1::MatchString::VARIABLE);
  std::string env_key = "polaris.source.test.key";
  match_string.mutable_value()->set_value(env_key);
  (*source->mutable_metadata())["env"] = match_string;
  route_rule_.InitFromPb(route_);
  source_service_info_.metadata_["env"] = "value";
  ASSERT_FALSE(route_rule_.MatchSource(&source_service_info_, parameters_));

  SystemVariables system_variables;
  ASSERT_TRUE(setenv(env_key.c_str(), "value", 1) == 0);
  route_rule_.FillSystemVariables(system_variables);
  ASSERT_TRUE(route_rule_.MatchSource(&source_service_info_, parameters_));
}

TEST_F(RouteRuleTest, SourceMatchMetadataParameterMatch) {
  v1::Source *source = route_.add_sources();
  source->mutable_namespace_()->set_value("*");
  source->mutable_service()->set_value("*");
  v1::MatchString match_string;
  match_string.set_value_type(v1::MatchString::PARAMETER);
  (*source->mutable_metadata())["key"] = match_string;
  route_rule_.InitFromPb(route_);
  ASSERT_FALSE(route_rule_.MatchSource(&source_service_info_, parameters_));
  source_service_info_.metadata_["key"] = "value";
  ASSERT_TRUE(route_rule_.MatchSource(&source_service_info_, parameters_));
  ASSERT_EQ(parameters_, "value");
}

}  // namespace polaris
