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

#include <google/protobuf/util/time_util.h>
#include <gtest/gtest.h>

#include "integration/common/environment.h"
#include "integration/common/integration_base.h"

#include "polaris/consumer.h"
#include "utils/time_clock.h"

namespace polaris {

class RuleRouterIntegrationTest : public IntegrationBase {
 protected:
  virtual void SetUp() {
    service_key_.namespace_ = "Test";
    service_key_.name_ = "rule.router.test" + std::to_string(Time::GetSystemTimeMs());
    service_.mutable_namespace_()->set_value(service_key_.namespace_);
    service_.mutable_name()->set_value(service_key_.name_);

    IntegrationBase::SetUp();

    // 创建Consumer对象
    consumer_ = ConsumerApi::CreateFromString(config_string_);
    ASSERT_TRUE(consumer_ != nullptr) << config_string_;

    routing_.mutable_service()->set_value(service_.name().value());
    routing_.mutable_namespace_()->set_value(service_.namespace_().value());
    routing_.mutable_service_token()->set_value(service_token_);
    next_port_ = 8000;
  }

  virtual void TearDown() {
    if (consumer_ != nullptr) {
      delete consumer_;
    }
    for (std::size_t i = 0; i < instances_.size(); ++i) {
      DeletePolarisServiceInstance(instances_[i]);
    }
    DeletePolarisServiceRouteRule(routing_);
    IntegrationBase::TearDown();
  }

  void CreateInstance(std::string env) {
    v1::Instance instance;
    std::string instance_id;
    instance.mutable_namespace_()->set_value(service_key_.namespace_);
    instance.mutable_service()->set_value(service_key_.name_);
    instance.mutable_service_token()->set_value(service_token_);
    instance.mutable_weight()->set_value(100);
    next_port_++;
    instance.mutable_host()->set_value("host" + std::to_string(next_port_));
    instance.mutable_port()->set_value(next_port_);
    (*instance.mutable_metadata())["env"] = env;
    AddPolarisServiceInstance(instance, instance_id);
    instance.mutable_id()->set_value(instance_id);
    instances_.push_back(instance);
  }

  void CreateInstances() {
    for (int i = 0; i < 10; ++i) {
      CreateInstance(i % 3 == 0 ? "base" : "test1");
    }
    WaitDataReady();
  }

  void WaitDataReady() {
    GetInstancesRequest request(service_key_);
    InstancesResponse *response;
    int sleep_count = 10;
    ReturnCode ret_code;
    std::size_t instances_size = 0;
    while (sleep_count-- > 0) {
      if ((ret_code = consumer_->GetAllInstances(request, response)) == kReturnOk) {
        instances_size = response->GetInstances().size();
        delete response;
        if (instances_size == instances_.size()) {
          break;
        }
      }
      sleep(1);
    }
    ASSERT_EQ(instances_size, instances_.size());
  }

 protected:
  ConsumerApi *consumer_;
  std::vector<v1::Instance> instances_;
  v1::Routing routing_;
  ServiceKey service_key_;
  Instance instance_;
  int next_port_;
};

TEST_F(RuleRouterIntegrationTest, EmptySourceRuleMatch) {
  // 创建规则
  v1::Route *route = routing_.add_inbounds();
  v1::Destination *destination = route->add_destinations();
  (*destination->mutable_metadata())["env"].set_type(v1::MatchString::EXACT);
  (*destination->mutable_metadata())["env"].mutable_value()->set_value("base");
  AddPolarisRouteRule(routing_);

  // 创建实例
  CreateInstances();

  GetOneInstanceRequest one_instance_request(service_key_);
  for (int i = 0; i < 100; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance_), kReturnOk);
    ASSERT_EQ(instance_.GetMetadata().find("env")->second, "base");
  }
}

TEST_F(RuleRouterIntegrationTest, WildcardSourceRuleMatch) {
  // 创建规则
  v1::Route *route = routing_.add_inbounds();
  v1::Source *source = route->add_sources();
  source->mutable_namespace_()->set_value("*");
  source->mutable_service()->set_value("*");
  v1::Destination *destination = route->add_destinations();
  (*destination->mutable_metadata())["env"].set_type(v1::MatchString::EXACT);
  (*destination->mutable_metadata())["env"].mutable_value()->set_value("test1");
  AddPolarisRouteRule(routing_);

  // 创建实例
  CreateInstances();

  GetOneInstanceRequest one_instance_request(service_key_);
  for (int i = 0; i < 100; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance_), kReturnOk);
    ASSERT_EQ(instance_.GetMetadata().find("env")->second, "test1");
  }
}

TEST_F(RuleRouterIntegrationTest, RuleMatchWithParameter) {
  // 创建规则
  v1::Route *route = routing_.add_inbounds();
  v1::Source *source = route->add_sources();
  (*source->mutable_metadata())["env"].set_value_type(v1::MatchString::PARAMETER);
  v1::Destination *destination = route->add_destinations();
  (*destination->mutable_metadata())["env"].set_value_type(v1::MatchString::PARAMETER);
  AddPolarisRouteRule(routing_);

  // 创建实例
  CreateInstances();

  GetOneInstanceRequest one_instance_request(service_key_);
  ServiceInfo service_info;
  for (int i = 0; i < 100; i++) {
    service_info.metadata_["env"] = i % 2 == 0 ? "test1" : "base";
    one_instance_request.SetSourceService(service_info);
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance_), kReturnOk);
    ASSERT_EQ(instance_.GetMetadata().find("env")->second, service_info.metadata_["env"]);
  }
}

class RuleRouterMultiEnvIntegrationTest : public RuleRouterIntegrationTest {
 protected:
  virtual void SetUp() {
    base_env_ = "POLARIS_BASE_ENV";
    int rc = setenv(base_env_.c_str(), "base", 1);
    ASSERT_EQ(rc, 0);
    env_ = "POLARIS_ENV";
    rc = setenv(env_.c_str(), "feature1", 1);
    ASSERT_EQ(rc, 0);
    RuleRouterIntegrationTest::SetUp();
  }

  virtual void TearDown() { RuleRouterIntegrationTest::TearDown(); }

 protected:
  std::string base_env_;
  std::string env_;
};

TEST_F(RuleRouterMultiEnvIntegrationTest, MultiEnvWithVariable) {
  // 创建规则
  v1::Route *route = routing_.add_inbounds();
  v1::Source *source = route->add_sources();
  (*source->mutable_metadata())["env"].set_value_type(v1::MatchString::PARAMETER);
  v1::Destination *destination = route->add_destinations();
  (*destination->mutable_metadata())["env"].set_value_type(v1::MatchString::PARAMETER);
  destination->mutable_priority()->set_value(0);
  destination = route->add_destinations();
  (*destination->mutable_metadata())["env"].set_value_type(v1::MatchString::VARIABLE);
  (*destination->mutable_metadata())["env"].mutable_value()->set_value(base_env_);
  destination->mutable_priority()->set_value(1);

  route = routing_.add_inbounds();
  destination = route->add_destinations();
  (*destination->mutable_metadata())["env"].set_value_type(v1::MatchString::VARIABLE);
  (*destination->mutable_metadata())["env"].mutable_value()->set_value(env_);
  destination->mutable_priority()->set_value(0);
  destination = route->add_destinations();
  (*destination->mutable_metadata())["env"].set_value_type(v1::MatchString::VARIABLE);
  (*destination->mutable_metadata())["env"].mutable_value()->set_value(base_env_);
  destination->mutable_priority()->set_value(1);

  AddPolarisRouteRule(routing_);

  // 创建实例
  CreateInstance("feature2");
  CreateInstances();

  GetOneInstanceRequest one_instance_request(service_key_);
  ServiceInfo service_info;
  for (int i = 0; i < 100; i++) {  // 透传env存在实例
    service_info.metadata_["env"] = i % 2 == 0 ? "test1" : "feature2";
    one_instance_request.SetSourceService(service_info);
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance_), kReturnOk);
    ASSERT_EQ(instance_.GetMetadata().find("env")->second, service_info.metadata_["env"]);
  }
  // 透传的env不存在，路由到base
  for (int i = 1; i < 10; i += 2) {
    service_info.metadata_["env"] = "feature" + std::to_string(i);
    one_instance_request.SetSourceService(service_info);
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance_), kReturnOk);
    ASSERT_EQ(instance_.GetMetadata().find("env")->second, "base");
  }

  // // 不传env
  service_info.metadata_.clear();
  one_instance_request.SetSourceService(service_info);
  for (int i = 1; i < 10; ++i) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance_), kReturnOk);
    ASSERT_EQ(instance_.GetMetadata().find("env")->second, "base");
  }

  // 创建feature1
  CreateInstance("feature1");
  WaitDataReady();
  for (int i = 1; i < 10; ++i) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance_), kReturnOk);
    ASSERT_EQ(instance_.GetMetadata().find("env")->second, "feature1");
  }

  // 传入别的metadata
  service_info.metadata_.clear();
  service_info.metadata_["abc"] = "123";
  one_instance_request.SetSourceService(service_info);
  for (int i = 1; i < 10; ++i) {
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance_), kReturnOk);
    ASSERT_EQ(instance_.GetMetadata().find("env")->second, "feature1");
  }
}

TEST_F(RuleRouterMultiEnvIntegrationTest, MatchDstService) {
  // 创建规则
  v1::Route *route = routing_.add_inbounds();
  v1::Source *source = route->add_sources();
  source->mutable_to_namespace()->set_value(service_key_.namespace_);
  source->mutable_to_service()->set_value(service_key_.name_);
  v1::Destination *destination = route->add_destinations();
  (*destination->mutable_metadata())["env"].mutable_value()->set_value("base");

  route = routing_.add_inbounds();
  destination = route->add_destinations();
  (*destination->mutable_metadata())["env"].mutable_value()->set_value("test1");

  AddPolarisRouteRule(routing_);

  // 创建实例
  CreateInstances();

  GetOneInstanceRequest one_instance_request(service_key_);
  for (int i = 0; i < 100; i++) {  // 透传env存在实例
    ASSERT_EQ(consumer_->GetOneInstance(one_instance_request, instance_), kReturnOk);
    ASSERT_EQ(instance_.GetMetadata().find("env")->second, "base");
  }
}

}  // namespace polaris
