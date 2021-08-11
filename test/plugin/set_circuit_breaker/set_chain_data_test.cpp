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

#include <string>

#include "mock/mock_local_registry.h"
#include "plugin/circuit_breaker/circuit_breaker.h"
#include "plugin/circuit_breaker/error_count.h"
#include "plugin/circuit_breaker/metric_window_manager.h"
#include "plugin/circuit_breaker/set_circuit_breaker_chain_data.h"
#include "test_context.h"
#include "test_utils.h"
#include "v1/circuitbreaker.pb.h"
#include "v1/response.pb.h"

using namespace std;

namespace polaris {

class SetChainDataTest : public ::testing::Test {
protected:
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    TestUtils::SetUpFakeTime();
    std::string err_msg, content = "enable:\n  true";
    default_config_ = Config::CreateFromString(content, err_msg);
    POLARIS_ASSERT(default_config_ != NULL && err_msg.empty());
    service_key_.namespace_ = "test_service_namespace";
    service_key_.name_      = "test_service_name";
    windows_ =
        new MetricWindowManager(context_, context_->GetContextImpl()->GetCircuitBreakerExecutor());

    record_     = new ServiceRecord();
    chain_data_ = new CircuitBreakSetChainData(service_key_, NULL, windows_, record_);

    monitor_reporter_ = context_->GetContextImpl()->GetMonitorReporter();

    dst_conf_ = new v1::DestinationSet();
    dst_conf_->set_type(v1::DestinationSet_Type_GLOBAL);
    dst_conf_->mutable_namespace_()->set_value("*");
    dst_conf_->mutable_service()->set_value("*");
    dst_conf_->set_scope(v1::DestinationSet_Scope_LABELS);

    v1::MatchString ms;
    ms.mutable_value()->set_value(".*");
    ms.set_type(v1::MatchString_MatchStringType_REGEX);
    (*dst_conf_->mutable_metadata())["k1"] = ms;
    v1::CbPolicy_ErrRateConfig *err_rate   = dst_conf_->mutable_policy()->mutable_errorrate();
    err_rate->mutable_enable()->set_value(true);
    err_rate->mutable_errorratetopreserved()->set_value(10);
    err_rate->mutable_errorratetoopen()->set_value(30);
    err_rate->mutable_requestvolumethreshold()->set_value(80);

    v1::CbPolicy_ErrRateConfig_SpecialConfig *sp_conf = err_rate->mutable_specials()->Add();
    sp_conf->mutable_type()->set_value("sp-err-1");
    google::protobuf::Int64Value *errv1 = sp_conf->mutable_errorcodes()->Add();
    errv1->set_value(1222);
    sp_conf->mutable_errorratetoopen()->set_value(20);
    sp_conf->mutable_errorratetopreserved()->set_value(5);

    v1::CbPolicy_SlowRateConfig *slow_rate = dst_conf_->mutable_policy()->mutable_slowrate();
    slow_rate->mutable_enable()->set_value(true);
    slow_rate->mutable_maxrt()->set_seconds(1);
    slow_rate->mutable_slowratetopreserved()->set_value(10);
    slow_rate->mutable_slowratetoopen()->set_value(20);
    slow_rate->mutable_slowratetopreserved()->set_value(10);
    dst_conf_->mutable_metricwindow()->set_seconds(60);
    dst_conf_->mutable_metricprecision()->set_value(12);
    dst_conf_->mutable_updateinterval()->set_seconds(5);

    v1::RecoverConfig *r = dst_conf_->mutable_recover();
    r->mutable_sleepwindow()->set_seconds(5);
    google::protobuf::UInt32Value *re1 = r->mutable_requestrateafterhalfopen()->Add();
    re1->set_value(20);
    google::protobuf::UInt32Value *re2 = r->mutable_requestrateafterhalfopen()->Add();
    re2->set_value(40);
  }

  virtual void TearDown() {
    if (default_config_ != NULL) delete default_config_;
    if (chain_data_ != NULL) {
      chain_data_->DecrementRef();
    }
    if (windows_ != NULL) {
      delete windows_;
    }
    if (dst_conf_ != NULL) {
      delete dst_conf_;
    }
    if (record_ != NULL) {
      delete record_;
      record_ = NULL;
    }
    TestUtils::TearDownFakeTime();
    if (context_ != NULL) delete context_;
  }

protected:
  Config *default_config_;
  ServiceKey service_key_;
  Context *context_;
  CircuitBreakSetChainData *chain_data_;
  MetricWindowManager *windows_;

  v1::DestinationSet *dst_conf_;
  ServiceRecord *record_;
  MonitorReporter *monitor_reporter_;
};

TEST_F(SetChainDataTest, TestJudgeResponseOpen) {
  v1::MetricResponse resp;
  resp.mutable_timestamp()->set_value(100000000000);
  v1::MetricResponse_MetricSum *sum         = resp.mutable_summaries()->Add();
  v1::MetricResponse_MetricSum_Value *req_v = sum->mutable_values()->Add();
  req_v->mutable_dimension()->set_type(v1::ReqCount);
  req_v->set_value(100);
  v1::MetricResponse_MetricSum_Value *err_v = sum->mutable_values()->Add();
  err_v->mutable_dimension()->set_type(v1::ErrorCount);
  err_v->set_value(100);

  ReturnCode ret_code;
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "|", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);
  SetCircuitBreakerUnhealthyInfo *info = chain_data_->GetSubSetUnhealthyInfo("|");
  ASSERT_TRUE(info != NULL);
  ASSERT_EQ(info->status, kCircuitBreakerOpen);

  err_v->set_value(0);
  resp.mutable_timestamp()->set_value(103000000000);
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "|", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);
  info = chain_data_->GetSubSetUnhealthyInfo("|");
  ASSERT_TRUE(info != NULL);
  ASSERT_EQ(info->status, kCircuitBreakerOpen);
  ASSERT_EQ(uint32_t(info->half_open_release_percent * 100), 0);

  resp.mutable_timestamp()->set_value(106000000000);
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "|", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);
  info = chain_data_->GetSubSetUnhealthyInfo("|");
  ASSERT_TRUE(info != NULL);
  ASSERT_EQ(info->status, kCircuitBreakerHalfOpen);
  ASSERT_EQ(uint32_t(info->half_open_release_percent * 100), 20);
  cout << "last_half_open_release_time:" << info->last_half_open_release_time << endl;

  err_v->set_value(0);
  resp.mutable_timestamp()->set_value(111000000000);
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "|", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);
  info = chain_data_->GetSubSetUnhealthyInfo("|");
  ASSERT_TRUE(info != NULL);
  ASSERT_EQ(info->status, kCircuitBreakerHalfOpen);
  ASSERT_EQ(uint32_t(info->half_open_release_percent * 100), 20);
  cout << "half_open_release_percent:" << info->half_open_release_percent << endl;
  cout << "last_half_open_release_time:" << info->last_half_open_release_time << endl;

  err_v->set_value(0);
  resp.mutable_timestamp()->set_value(171000000000);
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "|", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);
  info = chain_data_->GetSubSetUnhealthyInfo("|");
  ASSERT_TRUE(info != NULL);
  ASSERT_EQ(info->status, kCircuitBreakerHalfOpen);
  ASSERT_EQ(uint32_t(info->half_open_release_percent * 100), 40);
  cout << "half_open_release_percent:" << info->half_open_release_percent << endl;
  cout << "last_half_open_release_time:" << info->last_half_open_release_time << endl;

  err_v->set_value(0);
  resp.mutable_timestamp()->set_value(24000000000);
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "|", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);
  info = chain_data_->GetSubSetUnhealthyInfo("|");
  ASSERT_TRUE(info == NULL);

  std::map<ServiceKey, SetRecords> report_data;
  record_->ReportSetCircuitStat(report_data);

  ASSERT_EQ(report_data[service_key_].circuit_record_["|"].size(), 4);
  ASSERT_EQ(report_data[service_key_].circuit_record_["|"][0]->from_, kCircuitBreakerClose);
  ASSERT_EQ(report_data[service_key_].circuit_record_["|"][0]->to_, kCircuitBreakerOpen);
  ASSERT_TRUE(report_data[service_key_].circuit_record_["|"][0]->reason_.find(
                  "cased by err_rate") != std::string::npos);

  ASSERT_EQ(report_data[service_key_].circuit_record_["|"][1]->from_, kCircuitBreakerOpen);
  ASSERT_EQ(report_data[service_key_].circuit_record_["|"][1]->to_, kCircuitBreakerHalfOpen);

  ASSERT_EQ(report_data[service_key_].circuit_record_["|"][2]->from_, kCircuitBreakerHalfOpen);
  ASSERT_EQ(report_data[service_key_].circuit_record_["|"][2]->to_, kCircuitBreakerHalfOpen);

  ASSERT_EQ(report_data[service_key_].circuit_record_["|"][3]->from_, kCircuitBreakerHalfOpen);
  ASSERT_EQ(report_data[service_key_].circuit_record_["|"][3]->to_, kCircuitBreakerClose);
}

TEST_F(SetChainDataTest, TestJudgeResponsePreserved) {
  v1::MetricResponse resp;
  resp.mutable_timestamp()->set_value(100);
  v1::MetricResponse_MetricSum *sum         = resp.mutable_summaries()->Add();
  v1::MetricResponse_MetricSum_Value *req_v = sum->mutable_values()->Add();
  req_v->mutable_dimension()->set_type(v1::ReqCount);
  req_v->set_value(100);
  v1::MetricResponse_MetricSum_Value *err_v = sum->mutable_values()->Add();
  err_v->mutable_dimension()->set_type(v1::ErrorCount);
  err_v->set_value(15);

  ReturnCode ret_code;
  resp.mutable_timestamp()->set_value(14000000000);
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "#", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);

  SetCircuitBreakerUnhealthyInfo *info = chain_data_->GetSubSetUnhealthyInfo("#");
  ASSERT_TRUE(info != NULL);
  ASSERT_EQ(info->status, kCircuitBreakerPreserved);

  err_v->set_value(0);
  resp.mutable_timestamp()->set_value(24000000000);
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "#", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);
  info = chain_data_->GetSubSetUnhealthyInfo("#");
  ASSERT_TRUE(info == NULL);

  std::map<ServiceKey, SetRecords> report_data;
  record_->ReportSetCircuitStat(report_data);
  ASSERT_EQ(report_data[service_key_].circuit_record_["#"].size(), 2);
  ASSERT_EQ(report_data[service_key_].circuit_record_["#"][0]->from_, kCircuitBreakerClose);
  ASSERT_EQ(report_data[service_key_].circuit_record_["#"][0]->to_, kCircuitBreakerPreserved);
  ASSERT_EQ(report_data[service_key_].circuit_record_["#"][1]->from_, kCircuitBreakerPreserved);
  ASSERT_EQ(report_data[service_key_].circuit_record_["#"][1]->to_, kCircuitBreakerClose);
}

TEST_F(SetChainDataTest, TestJudgeResponsePreserve2) {
  v1::MetricResponse resp;
  resp.mutable_timestamp()->set_value(100);
  v1::MetricResponse_MetricSum *sum         = resp.mutable_summaries()->Add();
  v1::MetricResponse_MetricSum_Value *req_v = sum->mutable_values()->Add();
  req_v->mutable_dimension()->set_type(v1::ReqCount);
  req_v->set_value(100);
  v1::MetricResponse_MetricSum_Value *err_v = sum->mutable_values()->Add();
  err_v->mutable_dimension()->set_type(v1::ErrorCount);
  err_v->set_value(15);

  ReturnCode ret_code;
  resp.mutable_timestamp()->set_value(14000000000);
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "#", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);

  SetCircuitBreakerUnhealthyInfo *info = chain_data_->GetSubSetUnhealthyInfo("#");
  ASSERT_TRUE(info != NULL);
  ASSERT_EQ(info->status, kCircuitBreakerPreserved);

  err_v->set_value(35);
  resp.mutable_timestamp()->set_value(24000000000);
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "#", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);
  info = chain_data_->GetSubSetUnhealthyInfo("#");
  ASSERT_TRUE(info != NULL);
  ASSERT_EQ(info->status, kCircuitBreakerOpen);

  std::map<ServiceKey, SetRecords> report_data;
  record_->ReportSetCircuitStat(report_data);
  ASSERT_EQ(report_data[service_key_].circuit_record_["#"].size(), 2);
  ASSERT_EQ(report_data[service_key_].circuit_record_["#"][0]->from_, kCircuitBreakerClose);
  ASSERT_EQ(report_data[service_key_].circuit_record_["#"][0]->to_, kCircuitBreakerPreserved);
  ASSERT_EQ(report_data[service_key_].circuit_record_["#"][1]->from_, kCircuitBreakerPreserved);
  ASSERT_EQ(report_data[service_key_].circuit_record_["#"][1]->to_, kCircuitBreakerOpen);
}

TEST_F(SetChainDataTest, TestSlow) {
  v1::MetricResponse resp;
  resp.mutable_timestamp()->set_value(100);
  v1::MetricResponse_MetricSum *sum         = resp.mutable_summaries()->Add();
  v1::MetricResponse_MetricSum_Value *req_v = sum->mutable_values()->Add();
  req_v->mutable_dimension()->set_type(v1::ReqCount);
  req_v->set_value(100);
  v1::MetricResponse_MetricSum_Value *err_v = sum->mutable_values()->Add();
  err_v->mutable_dimension()->set_type(v1::ReqCountByDelay);
  err_v->set_value(25);

  ReturnCode ret_code;
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "#", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);

  SetCircuitBreakerUnhealthyInfo *info = chain_data_->GetSubSetUnhealthyInfo("#");
  ASSERT_TRUE(info != NULL);
  ASSERT_EQ(info->status, kCircuitBreakerOpen);

  std::map<ServiceKey, SetRecords> report_data;
  record_->ReportSetCircuitStat(report_data);
  ASSERT_TRUE(report_data[service_key_].circuit_record_["#"][0]->reason_.find(
                  "cased by slow_rate") != std::string::npos);
}

TEST_F(SetChainDataTest, TestCircuitBreakAll) {
  SubSetInfo subset;
  subset.subset_map_["k1"] = "v1";
  Labels labels;
  labels.labels_["l1"] = "v2";
  dst_conf_->set_scope(v1::DestinationSet_Scope_ALL);
  std::string version = "123";
  MetricWindow *win   = windows_->UpdateWindow(service_key_, subset, labels, version, dst_conf_,
                                             "testCbId", chain_data_);
  win->DecrementRef();
  v1::MetricResponse resp;
  resp.mutable_timestamp()->set_value(100);
  v1::MetricResponse_MetricSum *sum         = resp.mutable_summaries()->Add();
  v1::MetricResponse_MetricSum_Value *req_v = sum->mutable_values()->Add();
  req_v->mutable_dimension()->set_type(v1::ReqCount);
  req_v->set_value(100);
  v1::MetricResponse_MetricSum_Value *err_v = sum->mutable_values()->Add();
  err_v->mutable_dimension()->set_type(v1::ErrorCount);
  err_v->set_value(40);

  ReturnCode ret_code;
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "k1:v1#l1:v2", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);

  SetCircuitBreakerUnhealthyInfo *info = chain_data_->GetSubSetUnhealthyInfo("k1:v1#");
  ASSERT_TRUE(info != NULL);
  ASSERT_EQ(info->status, kCircuitBreakerOpen);
}

TEST_F(SetChainDataTest, SpecificErr) {
  SubSetInfo subset;
  subset.subset_map_["k1"] = "v1";
  Labels labels;
  labels.labels_["l1"] = "v2";
  std::string version  = "123";
  MetricWindow *win    = windows_->UpdateWindow(service_key_, subset, labels, version, dst_conf_,
                                             "testCbId", chain_data_);
  win->DecrementRef();
  v1::MetricResponse resp;
  resp.mutable_timestamp()->set_value(100);
  v1::MetricResponse_MetricSum *sum         = resp.mutable_summaries()->Add();
  v1::MetricResponse_MetricSum_Value *req_v = sum->mutable_values()->Add();
  req_v->mutable_dimension()->set_type(v1::ReqCount);
  req_v->set_value(100);
  v1::MetricResponse_MetricSum_Value *err_v = sum->mutable_values()->Add();
  err_v->mutable_dimension()->set_type(v1::ErrorCountByType);
  err_v->mutable_dimension()->set_value("sp-err-1");
  err_v->set_value(10);

  ReturnCode ret_code;
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "k1:v1#l1:v2", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);
  SetCircuitBreakerUnhealthyInfo *info = chain_data_->GetSubSetUnhealthyInfo("k1:v1#l1:v2");
  ASSERT_TRUE(info != NULL);
  ASSERT_EQ(info->status, kCircuitBreakerPreserved);

  err_v->set_value(25);
  ret_code = chain_data_->JudgeAndTranslateStatus(resp, "k1:v1#l1:v2", dst_conf_, "testCbId");
  ASSERT_EQ(ret_code, kReturnOk);
  info = chain_data_->GetSubSetUnhealthyInfo("k1:v1#l1:v2");
  ASSERT_TRUE(info != NULL);
  ASSERT_EQ(info->status, kCircuitBreakerOpen);

  std::map<ServiceKey, SetRecords> report_data;
  record_->ReportSetCircuitStat(report_data);
  ASSERT_TRUE(report_data[service_key_].circuit_record_["k1:v1#l1:v2"][0]->reason_.find(
                  "cased by specific_err") != std::string::npos);
}

}  // namespace polaris