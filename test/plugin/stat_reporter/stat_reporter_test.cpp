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

#include "plugin/stat_reporter/stat_reporter.h"

#include <gtest/gtest.h>
#include <pthread.h>

#include "utils/string_utils.h"

namespace polaris {

class StatReporterTest : public ::testing::Test {
protected:
  virtual void SetUp() {
    stat_reporter_ = new MonitorStatReporter();
    ASSERT_TRUE(stat_reporter_ != NULL);
  }

  virtual void TearDown() {
    if (stat_reporter_ != NULL) {
      delete stat_reporter_;
      stat_reporter_ = NULL;
    }
  }

protected:
  MonitorStatReporter *stat_reporter_;

  void CheckCollect(int thread) {
    ASSERT_TRUE(stat_reporter_->PerpareReport());
    std::map<ServiceKey, ServiceStat> report_data;
    stat_reporter_->CollectData(report_data);
    ASSERT_EQ(report_data.size(), 1);
    ServiceStat &service_stat = report_data.begin()->second;
    ASSERT_TRUE(service_stat.find("instance_0") != service_stat.end());
    InstanceStat &instance_stat = service_stat["instance_0"];
    ASSERT_TRUE(instance_stat.service_key_ == NULL);
    ASSERT_EQ(instance_stat.ret_code_stat_.size(), 2);
    ASSERT_EQ(instance_stat.ret_code_stat_[0].success_count_, 25 * thread);
    ASSERT_EQ(instance_stat.ret_code_stat_[0].success_delay_, 25 * thread);
    ASSERT_EQ(instance_stat.ret_code_stat_[2].success_count_, 25 * thread);
    ASSERT_EQ(instance_stat.ret_code_stat_[2].success_delay_, 25 * thread);
    ASSERT_TRUE(service_stat.find("instance_1") != service_stat.end());
    instance_stat = service_stat["instance_1"];
    ASSERT_EQ(instance_stat.ret_code_stat_[1].error_count_, 25 * thread);
    ASSERT_EQ(instance_stat.ret_code_stat_[1].error_delay_, 50 * thread);
    ASSERT_EQ(instance_stat.ret_code_stat_[3].error_count_, 25 * thread);
    ASSERT_EQ(instance_stat.ret_code_stat_[3].error_delay_, 50 * thread);
  }
};

void *ThreadFunc(void *args) {
  MonitorStatReporter *stat_reporter = static_cast<MonitorStatReporter *>(args);
  InstanceGauge instance_gauge;
  instance_gauge.service_namespace = "namespace";
  instance_gauge.service_name      = "service";
  for (int i = 0; i < 100; ++i) {
    instance_gauge.instance_id     = "instance_" + StringUtils::TypeToStr<int>(i % 2);
    instance_gauge.call_daley      = 1 + i % 2;
    instance_gauge.call_ret_code   = i % 4;
    instance_gauge.call_ret_status = (i % 2 == 0 ? kCallRetOk : kCallRetError);
    stat_reporter->ReportStat(instance_gauge);
  }
  return NULL;
}

TEST_F(StatReporterTest, SingleThreadTest) {
  for (int run = 0; run < 5; ++run) {
    ThreadFunc(stat_reporter_);
    ASSERT_TRUE(stat_reporter_->PerpareReport());
    CheckCollect(1);
  }
}

TEST_F(StatReporterTest, MultiThreadTest) {
  std::vector<pthread_t> thread_list;
  int thread_size = 5;
  pthread_t tid;
  for (int i = 0; i < thread_size; ++i) {
    pthread_create(&tid, NULL, ThreadFunc, stat_reporter_);
    thread_list.push_back(tid);
  }
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], NULL);
  }
  CheckCollect(thread_size);
}

struct ThreadArgWithStop {
  MonitorStatReporter *stat_reporter_;
  bool stop_;
};

void *ThreadFuncWithStop(void *params) {
  ThreadArgWithStop *args = static_cast<ThreadArgWithStop *>(params);
  InstanceGauge instance_gauge;
  instance_gauge.service_namespace = "namespace";
  instance_gauge.service_name      = "service";
  instance_gauge.instance_id       = "instance_0";
  int i                            = 0;
  while (!args->stop_) {
    instance_gauge.call_daley      = 1 + i % 2;
    instance_gauge.call_ret_code   = (i % 2 == 0 ? kReturnOk : kReturnNetworkFailed);
    instance_gauge.call_ret_status = (i % 2 == 0 ? kCallRetOk : kCallRetError);
    args->stat_reporter_->ReportStat(instance_gauge);
    i++;
  }
  return NULL;
}

TEST_F(StatReporterTest, MultiThreadWithStopTest) {
  std::vector<pthread_t> thread_list;
  int thread_size = 5;
  pthread_t tid;
  ThreadArgWithStop args;
  args.stat_reporter_ = stat_reporter_;
  args.stop_          = false;
  for (int i = 0; i < thread_size; ++i) {
    pthread_create(&tid, NULL, ThreadFuncWithStop, &args);
    thread_list.push_back(tid);
  }
  int count = 50000;
  while (count-- > 0) {
    while (!stat_reporter_->PerpareReport()) {
    }
    std::map<ServiceKey, ServiceStat> report_data;
    stat_reporter_->CollectData(report_data);
    if (!report_data.empty()) {
      ASSERT_EQ(report_data.size(), 1);
      ServiceStat &service_stat = report_data.begin()->second;
      ASSERT_TRUE(service_stat.find("instance_0") != service_stat.end());
      InstanceStat &instance_stat = service_stat["instance_0"];
      ASSERT_TRUE(instance_stat.service_key_ == NULL);
    }
  }
  args.stop_ = true;
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], NULL);
  }
}

}  // namespace polaris
