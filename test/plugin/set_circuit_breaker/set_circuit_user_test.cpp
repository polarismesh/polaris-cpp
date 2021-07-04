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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <pthread.h>
#include <stdint.h>

#include "context_internal.h"
#include "mock/fake_server_response.h"
#include "mock/mock_server_connector.h"
#include "test_utils.h"

#include "api/consumer_api.h"
#include "plugin/load_balancer/hash/hash_manager.h"
#include "test_context.h"

namespace polaris {

class SetCbUsrApiMockServerConnectorTest : public ::testing::Test {
protected:
  virtual void SetUp() {
    context_ = NULL;
    std::string err_msg, content =
                             "global:\n"
                             "  serverConnector:\n"
                             "    addresses:\n"
                             "      - 127.0.0.1:8081"
                             "\nconsumer:\n"
                             "  circuitBreaker:\n"
                             "    setCircuitBreaker:\n"
                             "      enable: true\n";
    config_ = Config::CreateFromString(content, err_msg);
    POLARIS_ASSERT(config_ != NULL && err_msg.empty());
    context_  = Context::Create(config_, kShareContextWithoutEngine);
    registry_ = TestContext::SetupMockLocalRegistry(context_);
    ASSERT_TRUE(context_ != NULL);

    service_key_.name_      = "cpp_test_service";
    service_key_.namespace_ = "cpp_test_namespace";

    SetRegistryData();
  }

  void SetRegistryData() {
    v1::Service *service = cb_pb_response_.mutable_service();
    service->mutable_namespace_()->set_value(service_key_.namespace_);
    service->mutable_name()->set_value(service_key_.name_);
    cb_pb_response_.set_type(v1::DiscoverResponse_DiscoverResponseType_CIRCUIT_BREAKER);
    v1::CircuitBreaker *cb = cb_pb_response_.mutable_circuitbreaker();
    cb->mutable_name()->set_value("testCb");
    cb->mutable_service()->set_value(service_key_.name_);
    cb->mutable_service_namespace()->set_value(service_key_.namespace_);
    cb->mutable_revision()->set_value("v2112");

    v1::CbRule *rule          = cb_pb_response_.mutable_circuitbreaker()->mutable_inbounds()->Add();
    v1::SourceMatcher *source = rule->mutable_sources()->Add();
    source->mutable_namespace_()->set_value("*");
    source->mutable_service()->set_value("*");
    v1::MatchString ms;
    ms.mutable_value()->set_value(".*");
    ms.set_type(v1::MatchString_MatchStringType_REGEX);
    (*source->mutable_labels())["l1"] = ms;

    v1::DestinationSet *dst = rule->mutable_destinations()->Add();
    dst->mutable_namespace_()->set_value("*");
    dst->mutable_service()->set_value("*");
    v1::MatchString ms1;
    ms1.mutable_value()->set_value("set1");
    ms1.set_type(v1::MatchString_MatchStringType_EXACT);
    (*dst->mutable_metadata())["set_flag"] = ms1;
    v1::CbPolicy_ErrRateConfig *err_rate   = dst->mutable_policy()->mutable_errorrate();
    err_rate->mutable_enable()->set_value(true);
    err_rate->mutable_errorratetopreserved()->set_value(10);

    v1::CbPolicy_ErrRateConfig_SpecialConfig *sp_conf = err_rate->mutable_specials()->Add();
    sp_conf->mutable_type()->set_value("sp-err-1");
    google::protobuf::Int64Value *errv1 = sp_conf->mutable_errorcodes()->Add();
    errv1->set_value(1222);
    sp_conf->mutable_errorratetoopen()->set_value(20);
    sp_conf->mutable_errorratetopreserved()->set_value(10);

    v1::CbPolicy_SlowRateConfig *slow_rate = dst->mutable_policy()->mutable_slowrate();
    slow_rate->mutable_enable()->set_value(true);
    slow_rate->mutable_maxrt()->set_seconds(1);
    slow_rate->mutable_slowratetopreserved()->set_value(10);
    dst->mutable_metricwindow()->set_seconds(60);
    dst->mutable_metricprecision()->set_value(12);
    dst->mutable_updateinterval()->set_seconds(5);

    service_data_ = ServiceData::CreateFromPb(&cb_pb_response_, polaris::kDataIsSyncing, 1);
    // ASSERT_TRUE(registry_->service_data_ != NULL);
  }

  virtual void TearDown() {
    if (context_ != NULL) {
      delete context_;
      context_ = NULL;
    }

    if (config_ != NULL) {
      delete config_;
      config_ = NULL;
    }

    if (service_data_ != NULL) {
      service_data_->DecrementAndGetRef();
    }

    if (registry_ != NULL) {
      registry_ = NULL;
    }
  }

public:
  void MockRegistryGetServiceDataRef(const ServiceKey &service_key, ServiceDataType /*data_type*/,
                                     ServiceData *&service_data) {
    if (service_key == service_key_) {
      if (service_data_ == NULL) {
      } else {
        service_data = service_data_;
        service_data->IncrementRef();
      }
    } else {
      printf("------------------not support\n");
      // service_data->IncrementRef();
      service_data = NULL;
    }
  }

protected:
  Config *config_;
  Context *context_;
  v1::DiscoverResponse cb_pb_response_;
  ServiceKey service_key_;
  MockLocalRegistry *registry_;

  ServiceData *service_data_;
};

TEST_F(SetCbUsrApiMockServerConnectorTest, TestUpdateServiceCallResult1) {
  EXPECT_CALL(*registry_,
              GetServiceDataWithRef(::testing::Eq(service_key_), ::testing::_, ::testing::_))
      .Times(::testing::Between(1, 10))
      .WillRepeatedly(::testing::DoAll(
          ::testing::Invoke(this,
                            &SetCbUsrApiMockServerConnectorTest::MockRegistryGetServiceDataRef),
          ::testing::Return(kReturnOk)));

  InstanceGauge gauge;
  gauge.source_service_key = service_key_;
  gauge.service_namespace  = service_key_.namespace_;
  gauge.service_name       = service_key_.name_;
  gauge.call_ret_status    = kCallRetOk;
  gauge.call_ret_code      = 0;
  gauge.call_daley         = 0;

  gauge.subset_["set_flag"] = "set1";
  gauge.labels_["l1"]       = "v1";

  ReturnCode ret;
  for (int i = 0; i < 5; ++i) {
    ret = polaris::ConsumerApiImpl::UpdateServiceCallResult(context_, gauge);
    ASSERT_EQ(ret, kReturnOk);
    sleep(1);
  }

  gauge.call_ret_code   = 10102;
  gauge.call_ret_status = kCallRetError;
  ret                   = polaris::ConsumerApiImpl::UpdateServiceCallResult(context_, gauge);
  ASSERT_EQ(ret, kReturnOk);

  gauge.call_ret_code   = 0;
  gauge.call_ret_status = kCallRetOk;
  gauge.call_daley      = 5000;
  ret                   = polaris::ConsumerApiImpl::UpdateServiceCallResult(context_, gauge);
  ASSERT_EQ(ret, kReturnOk);

  gauge.call_ret_code   = 1222;
  gauge.call_ret_status = kCallRetError;
  gauge.call_daley      = 0;
  ret                   = polaris::ConsumerApiImpl::UpdateServiceCallResult(context_, gauge);
  ASSERT_EQ(ret, kReturnOk);
}
}  // namespace polaris
