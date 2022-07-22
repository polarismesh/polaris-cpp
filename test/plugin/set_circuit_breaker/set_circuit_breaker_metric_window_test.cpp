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

#include <memory>
#include <string>

#include "mock/mock_local_registry.h"
#include "mock/mock_metric_connector.h"
#include "plugin/circuit_breaker/error_count.h"
#include "plugin/circuit_breaker/metric_window_manager.h"
#include "plugin/circuit_breaker/set_circuit_breaker_chain_data.h"
#include "test_context.h"
#include "test_utils.h"
#include "v1/circuitbreaker.pb.h"
#include "v1/response.pb.h"

namespace polaris {

class SetMetricWindowTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    TestUtils::SetUpFakeTime();
    std::string err_msg, content = "enable:\n  true";
    service_key_.namespace_ = "test_service_namespace";
    service_key_.name_ = "test_service_name";

    set_info_.subset_map_["k1"] = "v1";
    labels_.labels_["l1"] = "v1";

    SetDstConf();

    const v1::DestinationSet *ptr = dst_conf_;

    manager_ = new MetricWindowManager(context_, nullptr);

    record_ = new ServiceRecord();
    chain_data_ = new CircuitBreakSetChainData(service_key_, nullptr, manager_, record_);

    window_ = new MetricWindow(context_, service_key_, &set_info_, &labels_, ptr, "idtest", chain_data_);
    std::string version = "test01";

    executor_ = new CircuitBreakerExecutor(context_);
    metric_connector_ = new MockMetricConnector(executor_->GetReactor(), nullptr);
    executor_->SetMetricConnector(metric_connector_);

    window_->Init(executor_, version);
  }

  void SetDstConf() {
    dst_conf_ = new v1::DestinationSet();
    dst_conf_->set_type(v1::DestinationSet_Type_GLOBAL);
    dst_conf_->mutable_namespace_()->set_value("*");
    dst_conf_->mutable_service()->set_value("*");
    dst_conf_->set_scope(v1::DestinationSet_Scope_LABELS);

    v1::MatchString ms;
    ms.mutable_value()->set_value(".*");
    ms.set_type(v1::MatchString_MatchStringType_REGEX);
    (*dst_conf_->mutable_metadata())["k1"] = ms;
    v1::CbPolicy_ErrRateConfig *err_rate = dst_conf_->mutable_policy()->mutable_errorrate();
    err_rate->mutable_enable()->set_value(true);
    err_rate->mutable_errorratetopreserved()->set_value(10);
    err_rate->mutable_errorratetoopen()->set_value(30);

    v1::CbPolicy_ErrRateConfig_SpecialConfig *sp = err_rate->mutable_specials()->Add();
    google::protobuf::Int64Value *v1 = sp->mutable_errorcodes()->Add();
    v1->set_value(131232);
    sp->mutable_type()->set_value("sp_err_type1");

    v1::CbPolicy_SlowRateConfig *slow_rate = dst_conf_->mutable_policy()->mutable_slowrate();
    slow_rate->mutable_enable()->set_value(true);
    slow_rate->mutable_maxrt()->set_seconds(1);
    slow_rate->mutable_slowratetopreserved()->set_value(10);
    slow_rate->mutable_slowratetoopen()->set_value(20);
    slow_rate->mutable_slowratetopreserved()->set_value(10);
    dst_conf_->mutable_metricwindow()->set_seconds(60);
    dst_conf_->mutable_metricprecision()->set_value(12);
    dst_conf_->mutable_updateinterval()->set_seconds(2);

    v1::RecoverConfig *r = dst_conf_->mutable_recover();
    r->mutable_sleepwindow()->set_seconds(5);
    google::protobuf::UInt32Value *re1 = r->mutable_requestrateafterhalfopen()->Add();
    re1->set_value(20);
    google::protobuf::UInt32Value *re2 = r->mutable_requestrateafterhalfopen()->Add();
    re2->set_value(40);
  }

  virtual void TearDown() {
    if (context_ != nullptr) delete context_;
    if (dst_conf_ != nullptr) {
      delete dst_conf_;
    }
    if (window_ != nullptr) {
      window_->DecrementRef();
    }

    if (manager_ != nullptr) {
      delete manager_;
    }
    if (executor_ != nullptr) {
      executor_->GetReactor().Stop();
      delete executor_;
    }
    if (record_ != nullptr) {
      delete record_;
      record_ = nullptr;
    }
    if (chain_data_ != nullptr) {
      chain_data_->DecrementRef();
    }
    TestUtils::TearDownFakeTime();
  }

 public:
  ReturnCode MockOnSuccessRespCode500(v1::MetricRequest * /*request*/, uint64_t /*timeout*/,
                                      grpc::RpcCallback<v1::MetricResponse> *callback) {
    v1::MetricResponse *resp = new v1::MetricResponse();
    resp->mutable_code()->set_value(5001001);
    callback->OnSuccess(resp);
    return kReturnOk;
  }

 protected:
  ServiceKey service_key_;
  Context *context_;

  SubSetInfo set_info_;
  Labels labels_;
  v1::DestinationSet *dst_conf_;

  MetricWindow *window_;

  MetricWindowManager *manager_;
  CircuitBreakerExecutor *executor_;
  MockMetricConnector *metric_connector_;

  CircuitBreakSetChainData *chain_data_;
  ServiceRecord *record_;
};

TEST_F(SetMetricWindowTest, Test1) {
  EXPECT_CALL(*metric_connector_, IsMetricInit(::testing::_))
      .Times(::testing::Between(1, 10))
      .WillRepeatedly(::testing::Return(true));
  EXPECT_CALL(*metric_connector_, Report(::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(metric_connector_, &MockMetricConnector::OnResponse200<v1::MetricRequest>),
                           ::testing::Return(kReturnOk)));
  EXPECT_CALL(*metric_connector_, Query(::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(1))
      .WillRepeatedly(::testing::DoAll(
          ::testing::Invoke(metric_connector_, &MockMetricConnector::OnResponse200<v1::MetricQueryRequest>),
          ::testing::Return(kReturnOk)));
  InstanceGauge gauge;
  gauge.source_service_key = new ServiceKey(service_key_);
  gauge.service_key_ = service_key_;
  gauge.call_ret_status = kCallRetOk;
  gauge.call_ret_code = 0;
  gauge.call_daley = 0;

  gauge.subset_ = new std::map<std::string, std::string>();
  gauge.subset_->insert(std::make_pair("set_flag", "set1"));
  gauge.labels_ = new std::map<std::string, std::string>();
  gauge.labels_->insert(std::make_pair("l1", "v1"));
  window_->AddCount(gauge);

  v1::MetricInitRequest *req = window_->AssembleInitReq();
  ASSERT_TRUE(req != nullptr);
  delete req;

  v1::MetricRequest *req1 = window_->AssembleReportReq();
  ASSERT_TRUE(req1 != nullptr);

  MetricInitCallBack *cb = new MetricInitCallBack(window_);
  delete cb;

  MetricReportCallBack *cb1 = new MetricReportCallBack(window_, *req1);

  v1::MetricResponse *resp = new v1::MetricResponse();
  resp->mutable_code()->set_value(500000);
  cb1->OnSuccess(resp);

  TestUtils::FakeNowIncrement(3000);
  executor_->GetReactor().RunOnce();
  delete cb1;
  delete req1;
}

TEST_F(SetMetricWindowTest, Test2) {
  EXPECT_CALL(*metric_connector_, IsMetricInit(::testing::_))
      .Times(::testing::Between(1, 10))
      .WillRepeatedly(::testing::Return(true));
  EXPECT_CALL(*metric_connector_, Report(::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(3))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(metric_connector_, &MockMetricConnector::OnResponse500<v1::MetricRequest>),
                           ::testing::Return(kReturnOk)));
  EXPECT_CALL(*metric_connector_, Query(::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(1))
      .WillRepeatedly(::testing::DoAll(
          ::testing::Invoke(metric_connector_, &MockMetricConnector::OnResponse200<v1::MetricQueryRequest>),
          ::testing::Return(kReturnOk)));
  TestUtils::FakeNowIncrement(3000);
  executor_->GetReactor().RunOnce();
}

}  // namespace polaris