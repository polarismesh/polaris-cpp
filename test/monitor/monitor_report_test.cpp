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

#include "monitor/monitor_reporter.h"

#include <gtest/gtest.h>

#include "mock/fake_server_response.h"
#include "test_context.h"

namespace polaris {

class MonitorReportTest : public ::testing::Test {
protected:
  virtual void SetUp() {
    context_          = TestContext::CreateContext();
    monitor_reporter_ = context_->GetContextImpl()->GetMonitorReporter();
    local_registry_   = context_->GetLocalRegistry();
  }

  virtual void TearDown() {
    monitor_reporter_ = NULL;
    local_registry_   = NULL;
    if (context_ != NULL) {
      delete context_;
      context_ = NULL;
    }
  }

protected:
  Context* context_;
  MonitorReporter* monitor_reporter_;
  LocalRegistry* local_registry_;
};

void CreateServiceStat(std::map<ServiceKey, ServiceStat>& stat_data) {
  for (int i = 0; i < 3; i++) {
    ServiceKey service_key    = {"stat_namespace", "stat_service" + StringUtils::TypeToStr(i)};
    ServiceStat& service_stat = stat_data[service_key];
    for (int j = 0; j <= i; j++) {
      std::string instance_id     = "instance_" + StringUtils::TypeToStr(j);
      InstanceStat& instance_stat = service_stat[instance_id];
      for (int k = 0; k <= i + j; ++k) {
        InstanceCodeStat& code_stat = instance_stat.ret_code_stat_[k];
        code_stat.success_count_    = k;
        code_stat.success_delay_    = k * 10;
        code_stat.error_count_      = k % 2;
        code_stat.error_delay_      = (k % 2) * 20;
      }
    }
  }
}

void CreateSetServiceStat(std::map<ServiceKey, SetRecords>& set_circuit_map) {
  for (int i = 0; i < 3; i++) {
    ServiceKey service_key           = {"Test", "test_name_" + StringUtils::TypeToStr(i)};
    SetRecords& service_stat         = set_circuit_map[service_key];
    CircuitChangeRecord* record      = new CircuitChangeRecord();
    record->change_seq_              = 1;
    record->circuit_breaker_conf_id_ = "test_id";
    record->from_                    = kCircuitBreakerClose;
    record->to_                      = kCircuitBreakerOpen;
    record->reason_                  = "";
    service_stat.circuit_record_["k1:set1"].push_back(record);
  }
}

TEST_F(MonitorReportTest, BuildServiceStatWithServiceNotFound) {
  std::map<ServiceKey, ServiceStat> stat_data;
  CreateServiceStat(stat_data);
  google::protobuf::RepeatedField<v1::ServiceStatistics> report_data;
  monitor_reporter_->BuildServiceStat(stat_data, report_data);
  ASSERT_EQ(report_data.size(), 0);
}

TEST_F(MonitorReportTest, BuildServiceStatWithInstanceNotFound) {
  ServiceKey service_key = {"stat_namespace", "stat_service" + StringUtils::TypeToStr(2)};
  FakeServer::InitService(local_registry_, service_key, 1, false);
  std::map<ServiceKey, ServiceStat> stat_data;
  // i = 2, j = 0
  CreateServiceStat(stat_data);
  google::protobuf::RepeatedField<v1::ServiceStatistics> report_data;
  monitor_reporter_->BuildServiceStat(stat_data, report_data);
  // 服务2 实例0 有三个返回码：0, 1, 2
  ASSERT_EQ(report_data.size(), 3);  // 成功2(k=1,2) + 失败1(k=1)
}

TEST_F(MonitorReportTest, BuildServiceStat) {
  ServiceKey service_key = {"stat_namespace", "stat_service" + StringUtils::TypeToStr(2)};
  FakeServer::InitService(local_registry_, service_key, 3, false);
  std::map<ServiceKey, ServiceStat> stat_data;
  // i = 2, j = 0, 1, 2
  CreateServiceStat(stat_data);
  google::protobuf::RepeatedField<v1::ServiceStatistics> report_data;
  monitor_reporter_->BuildServiceStat(stat_data, report_data);
  // 实例0: 3 = 成功2(k=1,2) + 失败1(k=1)
  // 实例1: 5 = 成功3(k=1,2,3) + 失败2(k=1,3)
  // 实例2: 6 = 成功4(k=1,2,3,4) + 失败2(k=1,3)
  ASSERT_EQ(report_data.size(), 3 + 5 + 6);
}

TEST_F(MonitorReportTest, BuildSetCircuitStat) {
  std::map<ServiceKey, SetRecords> set_circuit_map;
  CreateSetServiceStat(set_circuit_map);
  google::protobuf::RepeatedField<v1::ServiceCircuitbreak> report_data;
  monitor_reporter_->BuildSetCircuitStat(set_circuit_map, report_data);
  ASSERT_EQ(report_data.size(), 3);
  ASSERT_EQ(report_data[0].subset_circuitbreak().size(), 1);
}

}  // namespace polaris
