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

#include "plugin/circuit_breaker/set_circuit_breaker.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "mock/mock_local_registry.h"
#include "plugin/circuit_breaker/error_count.h"
#include "test_context.h"
#include "test_utils.h"
#include "v1/circuitbreaker.pb.h"
#include "v1/response.pb.h"

namespace polaris {

class SetCircuitBreakerChainTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    TestUtils::SetUpFakeTime();
    std::string err_msg, content = "enable:\n  true";
    default_config_ = Config::CreateFromString(content, err_msg);
    POLARIS_ASSERT(default_config_ != nullptr && err_msg.empty());
    service_key_.namespace_ = "test_service_namespace";
    service_key_.name_ = "test_service_name";
    mock_local_registry_ = TestContext::SetupMockLocalRegistry(context_);
    chain_ = new SetCircuitBreakerImpl(service_key_);
    SetRegistryData();
  }

  virtual void TearDown() {
    if (default_config_ != nullptr) delete default_config_;
    if (chain_ != nullptr) delete chain_;
    if (mock_local_registry_ != nullptr) {
      // mock_local_registry_->DeleteNotify();
      for (size_t i = 0; i < mock_local_registry_->service_data_list_.size(); ++i) {
        mock_local_registry_->service_data_list_[i]->DecrementAndGetRef();
      }
    }
    TestUtils::TearDownFakeTime();
    if (context_ != nullptr) delete context_;
  }

  void SetRegistryData() {
    response.mutable_service()->mutable_namespace_()->set_value("test");
    response.mutable_service()->mutable_name()->set_value("name1");
    response.set_type(v1::DiscoverResponse_DiscoverResponseType_CIRCUIT_BREAKER);
    response.mutable_circuitbreaker()->mutable_name()->set_value("test");
    response.mutable_circuitbreaker()->mutable_service()->set_value("name1");
    response.mutable_circuitbreaker()->mutable_revision()->set_value("v2112");
    v1::CbRule* rule = response.mutable_circuitbreaker()->mutable_inbounds()->Add();
    v1::SourceMatcher* source = rule->mutable_sources()->Add();
    source->mutable_namespace_()->set_value("*");
    source->mutable_service()->set_value("*");
    v1::MatchString ms;
    ms.mutable_value()->set_value(".*");
    ms.set_type(v1::MatchString_MatchStringType_REGEX);
    (*source->mutable_labels())["l1"] = ms;
    v1::DestinationSet* dst = rule->mutable_destinations()->Add();
    dst->mutable_namespace_()->set_value("*");
    dst->mutable_service()->set_value("*");
    (*dst->mutable_metadata())["k1"] = ms;
    v1::CbPolicy_ErrRateConfig* err_rate = dst->mutable_policy()->mutable_errorrate();
    err_rate->mutable_enable()->set_value(true);
    err_rate->mutable_errorratetopreserved()->set_value(10);
    v1::CbPolicy_SlowRateConfig* slow_rate = dst->mutable_policy()->mutable_slowrate();
    slow_rate->mutable_enable()->set_value(true);
    slow_rate->mutable_maxrt()->set_seconds(1);
    slow_rate->mutable_slowratetopreserved()->set_value(10);
    dst->mutable_metricwindow()->set_seconds(60);
    dst->mutable_metricprecision()->set_value(12);
    dst->mutable_updateinterval()->set_seconds(5);

    ReturnCode ret = chain_->Init(default_config_, context_);
    ASSERT_EQ(ret, kReturnOk);
  }

 protected:
  Config* default_config_;
  ServiceKey service_key_;
  Context* context_;
  MockLocalRegistry* mock_local_registry_;
  SetCircuitBreakerImpl* chain_;

  v1::DiscoverResponse response;

  ServiceData* circuit_pb_data_;
};

TEST_F(SetCircuitBreakerChainTest, TestRealTime01) {
  SubSetInfo sub;
  sub.subset_map_["k1"] = "v1";
  sub.subset_map_["k2"] = "v2";
  std::string ret = sub.GetSubInfoStrId();
  ASSERT_EQ(ret, "k1:v1|k2:v2");
}

}  // namespace polaris