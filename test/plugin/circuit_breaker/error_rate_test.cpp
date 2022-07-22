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

#include "plugin/circuit_breaker/error_rate.h"

#include <gtest/gtest.h>

#include <math.h>

#include <string>

#include "mock/mock_local_registry.h"
#include "model/constants.h"
#include "plugin/circuit_breaker/chain.h"
#include "test_context.h"
#include "test_utils.h"
#include "utils/utils.h"

namespace polaris {

const int kTestMultiThreadGetOrCreateTime = 100;

// ===============================================================
// 错误率插件熔断测试
class ErrorRateCircuitBreakerTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    service_key_ = context_->GetContextImpl()->GetDiscoverService().service_;
    TestUtils::SetUpFakeTime();
    default_config_ = Config::CreateEmptyConfig();
    ASSERT_TRUE(default_config_ != nullptr);
    error_rate_circuit_breaker_ = new ErrorRateCircuitBreaker();
    error_rate_circuit_breaker_->Init(default_config_, context_);
    instance_gauge_.instance_id = "uuid-2";
    instance_gauge_.call_daley = 100;
    instance_gauge_.call_ret_code = 42;
    instance_gauge_.call_ret_status = kCallRetOk;
    chain_data_ = new CircuitBreakerChainData();
    CircuitBreakerPluginData plugin_data;
    plugin_data.plugin_name = "errorRate";
    plugin_data.request_after_half_open = constants::kRequestCountAfterHalfOpenDefault;
    chain_data_->AppendPluginData(plugin_data);
    circuit_breaker_status_ = new InstancesCircuitBreakerStatus(chain_data_, 1, service_key_,
                                                                context_->GetContextImpl()->GetServiceRecord(), true);
    default_bucket_time_ =
        ceilf(static_cast<float>(constants::kMetricStatTimeWindowDefault) / constants::kMetricNumBucketsDefault);
  }

  virtual void TearDown() {
    if (error_rate_circuit_breaker_ != nullptr) delete error_rate_circuit_breaker_;
    if (circuit_breaker_status_ != nullptr) delete circuit_breaker_status_;
    if (chain_data_ != nullptr) delete chain_data_;
    if (default_config_ != nullptr) delete default_config_;
    TestUtils::TearDownFakeTime();
    if (context_ != nullptr) delete context_;
  }

  static void *MultiThreadGetOrCreateErrorRateMap(void *arg);

  static void *MultiThreadGetOrCreateErrorRateInstance(void *arg);

  static void *MultiThreadRealTimeErrorRate(void *arg);

 protected:
  ErrorRateCircuitBreaker *error_rate_circuit_breaker_;
  InstanceGauge instance_gauge_;
  Config *default_config_;
  ServiceKey service_key_;
  Context *context_;
  uint64_t default_bucket_time_;
  CircuitBreakerChainData *chain_data_;
  InstancesCircuitBreakerStatus *circuit_breaker_status_;
};

// 单线程创建或获取统计状态
TEST_F(ErrorRateCircuitBreakerTest, OneThreadGetOrCreateStatus) {
  // 首次获取
  std::string instance_id = "instance";
  ErrorRateStatus *status = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_id);
  ASSERT_EQ(status->status, kCircuitBreakerClose);
  ASSERT_TRUE(status->buckets != nullptr);
  ASSERT_EQ(status->last_update_time, 0);

  // 修改数据
  status->buckets[0].bucket_time = 1;
  status->buckets[0].error_count = 2;
  status->buckets[0].total_count = 3;
  status->last_update_time = 2;

  // 再次获取，是同一个状态对象
  ErrorRateStatus *status2 = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_id);
  ASSERT_EQ(status, status2);
  ASSERT_EQ(status2->buckets[0].bucket_time, 1);
  ASSERT_EQ(status2->buckets[0].error_count, 2);
  ASSERT_EQ(status2->buckets[0].total_count, 3);
  ASSERT_EQ(status2->last_update_time, 2);
}

// 每个线程创建不同的实例
void *ErrorRateCircuitBreakerTest::MultiThreadGetOrCreateErrorRateMap(void *arg) {
  ErrorRateCircuitBreakerTest *test = static_cast<ErrorRateCircuitBreakerTest *>(arg);
  pthread_t tid = pthread_self();
  std::stringstream instance_id;
  instance_id << "instance_" << tid;
  for (int i = 0; i < kTestMultiThreadGetOrCreateTime; ++i) {
    ErrorRateStatus *status = test->error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_id.str());
    EXPECT_EQ(status->last_update_time, i);
    // 单线程修改数据
    status->last_update_time++;
  }
  return nullptr;
}

// 多线程创建或获取统计状态
TEST_F(ErrorRateCircuitBreakerTest, MultiThreadGetOrCreateStatusMap) {
  int thread_num = 5;
  std::vector<pthread_t> thread_id_list;
  pthread_t tid;
  // 多线程并发查询和修改Map中的数据，每给线程只修改自己对应的实例
  for (int i = 0; i < thread_num; i++) {
    pthread_create(&tid, nullptr, MultiThreadGetOrCreateErrorRateMap, this);
    thread_id_list.push_back(tid);
  }
  EXPECT_EQ(thread_id_list.size(), thread_num);
  for (size_t i = 0; i < thread_id_list.size(); i++) {
    pthread_join(thread_id_list[i], nullptr);
  }
  for (int i = 0; i < thread_num; ++i) {
    std::stringstream instance_id;
    instance_id << "instance_" << thread_id_list[i];
    ErrorRateStatus *status = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_id.str());
    ASSERT_EQ(status->last_update_time, kTestMultiThreadGetOrCreateTime);
  }
}

// 每个线程创建相同的实例
void *ErrorRateCircuitBreakerTest::MultiThreadGetOrCreateErrorRateInstance(void *arg) {
  ErrorRateCircuitBreakerTest *test = static_cast<ErrorRateCircuitBreakerTest *>(arg);
  for (int i = 0; i < kTestMultiThreadGetOrCreateTime; ++i) {
    std::stringstream instance_id;
    instance_id << "instance_" << i;
    ErrorRateStatus *status = test->error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_id.str());
    // 并发修改数据
    status->last_update_time++;
  }
  return nullptr;
}

// 多线程创建或获取统计状态
TEST_F(ErrorRateCircuitBreakerTest, MultiThreadGetOrCreateStatusInstance) {
  int thread_num = 5;
  std::vector<pthread_t> thread_id_list;
  pthread_t tid;
  // 多线程并发创建修改相同实例数据
  for (int i = 0; i < thread_num; i++) {
    pthread_create(&tid, nullptr, MultiThreadGetOrCreateErrorRateInstance, this);
    thread_id_list.push_back(tid);
  }
  EXPECT_EQ(thread_id_list.size(), thread_num);
  for (size_t i = 0; i < thread_id_list.size(); i++) {
    pthread_join(thread_id_list[i], nullptr);
  }
  for (int i = 0; i < kTestMultiThreadGetOrCreateTime; ++i) {
    std::stringstream instance_id;
    instance_id << "instance_" << i;
    ErrorRateStatus *status = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_id.str());
    ASSERT_EQ(status->last_update_time, thread_num);
  }
}

// 测试数达标
TEST_F(ErrorRateCircuitBreakerTest, TestRequestVolumeThreshold) {
  error_rate_circuit_breaker_->Init(default_config_, context_);
  // 区间内错误数不达标，但错误率不达标 60s 12bucket 10请求 0.5错误率
  instance_gauge_.call_ret_status = kCallRetError;
  ErrorRateStatus *error_rate_status =
      error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_gauge_.instance_id);
  uint64_t current_time;
  for (int i = 0; i < constants::kRequestVolumeThresholdDefault - 1; ++i) {
    current_time = Time::GetCoarseSteadyTimeMs();
    uint64_t bucket_time = current_time / default_bucket_time_;
    int bucket_index = bucket_time % constants::kMetricNumBucketsDefault;
    error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
    ASSERT_EQ(error_rate_status->status, kCircuitBreakerClose);
    ASSERT_EQ(error_rate_status->buckets[bucket_index].bucket_time, current_time / default_bucket_time_);
    ASSERT_EQ(error_rate_status->buckets[bucket_index].error_count, 1);
    ASSERT_EQ(error_rate_status->buckets[bucket_index].total_count, 1);
    TestUtils::FakeNowIncrement(default_bucket_time_);
  }

  // 请求数和失败率都达标
  error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerOpen);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
}

// 单线程错误数计算
TEST_F(ErrorRateCircuitBreakerTest, SingleThreadErrorCount) {
  error_rate_circuit_breaker_->Init(default_config_, context_);
  ErrorRateStatus *error_rate_status =
      error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_gauge_.instance_id);

  // 错误数很多，但一直不连续，每当差一次达到阈值又成功一次
  instance_gauge_.call_ret_status = kCallRetOk;
  error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  uint64_t request_count = constants::kRequestVolumeThresholdDefault * 2;
  for (uint64_t i = 0; i < request_count; i++) {
    error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
    TestUtils::FakeNowIncrement(1000);
    instance_gauge_.call_ret_status = i % 2 == 0 ? kCallRetOk : kCallRetError;
    error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    ASSERT_EQ(error_rate_status->last_update_time, 0);
    ASSERT_EQ(error_rate_status->status, kCircuitBreakerClose);
  }
  // 超过默认阈值50%，熔断
  instance_gauge_.call_ret_status = kCallRetError;
  error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerOpen);

  // 变成打开状态后，过了配置的时间变成半开状态
  TestUtils::FakeNowIncrement(constants::kHalfOpenSleepWindowDefault);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerHalfOpen);

  // 半开后错误一定次数立刻重新熔断
  instance_gauge_.call_ret_status = kCallRetError;
  request_count = constants::kRequestCountAfterHalfOpenDefault - constants::kSuccessCountAfterHalfOpenDefault + 1;
  for (uint64_t i = 0; i < request_count; i++) {
    TestUtils::FakeNowIncrement(100);
    ASSERT_EQ(error_rate_status->status, kCircuitBreakerHalfOpen);
    error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  }
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerOpen);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());

  // 重新半开
  TestUtils::FakeNowIncrement(constants::kHalfOpenSleepWindowDefault - 1);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerOpen);
  TestUtils::FakeNowIncrement(1);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerHalfOpen);

  // 全部放量结束后，成功数不够重新熔断
  for (uint64_t i = 1; i < constants::kRequestCountAfterHalfOpenDefault; i++) {
    TestUtils::FakeNowIncrement(100);
    instance_gauge_.call_ret_status = i < constants::kSuccessCountAfterHalfOpenDefault ? kCallRetOk : kCallRetError;
    error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    ASSERT_EQ(error_rate_status->status, kCircuitBreakerHalfOpen);
  }
  TestUtils::FakeNowIncrement(constants::kHalfOpenSleepWindowDefault);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_LE(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerHalfOpen);
  instance_gauge_.call_ret_status = kCallRetError;
  error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerOpen);

  // 重新半开
  TestUtils::FakeNowIncrement(constants::kHalfOpenSleepWindowDefault);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerHalfOpen);

  // 成功请求数达到条件，恢复
  instance_gauge_.call_ret_status = kCallRetOk;
  request_count = constants::kSuccessCountAfterHalfOpenDefault;
  for (uint64_t i = 0; i < request_count; i++) {
    TestUtils::FakeNowIncrement(100);
    error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    ASSERT_EQ(error_rate_status->status, kCircuitBreakerHalfOpen);
  }
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerClose);
}

// 验证半开放量统计不会被清理，超过统计窗口也能正常恢复
TEST_F(ErrorRateCircuitBreakerTest, HalfOpenReportStat) {
  error_rate_circuit_breaker_->Init(default_config_, context_);
  ErrorRateStatus *error_rate_status =
      error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_gauge_.instance_id);
  instance_gauge_.call_ret_status = kCallRetOk;
  error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  uint64_t request_count = constants::kRequestVolumeThresholdDefault * 2;
  for (uint64_t i = 0; i < request_count; i++) {
    error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
    TestUtils::FakeNowIncrement(1000);
    instance_gauge_.call_ret_status = i % 2 == 0 ? kCallRetOk : kCallRetError;
    error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    ASSERT_EQ(error_rate_status->status, kCircuitBreakerClose);
    ASSERT_EQ(error_rate_status->last_update_time, 0);
  }
  // 超过默认阈值50%，熔断
  instance_gauge_.call_ret_status = kCallRetError;
  error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerOpen);

  // 变成熔断状态后，过了配置的时间变成半开状态
  TestUtils::FakeNowIncrement(constants::kHalfOpenSleepWindowDefault);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerHalfOpen);

  // 成功请求数达到条件，恢复
  instance_gauge_.call_ret_status = kCallRetOk;
  request_count = constants::kSuccessCountAfterHalfOpenDefault;
  for (uint64_t i = 0; i < request_count; i++) {
    TestUtils::FakeNowIncrement(10 * 1000);  // 10s才上报一次
    error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    ASSERT_EQ(error_rate_status->status, kCircuitBreakerHalfOpen);
  }
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerClose);
}

struct ErrorRateRealTimeArg {
  ErrorRateRealTimeArg(ErrorRateCircuitBreakerTest *test, InstanceGauge gauge, int report_time, int ret_code_mod)
      : test_(test), gauge_(gauge), report_time_(report_time), ret_code_mod_(ret_code_mod) {}
  ErrorRateCircuitBreakerTest *test_;
  InstanceGauge gauge_;
  int report_time_;
  int ret_code_mod_;
};

void *ErrorRateCircuitBreakerTest::MultiThreadRealTimeErrorRate(void *arg) {
  ErrorRateRealTimeArg *test_arg = static_cast<ErrorRateRealTimeArg *>(arg);
  for (int i = 0; i < test_arg->report_time_; ++i) {
    TestUtils::FakeNowIncrement(2 * i);
    test_arg->gauge_.call_ret_status = i % test_arg->ret_code_mod_ == 0 ? kCallRetOk : kCallRetError;
    test_arg->test_->error_rate_circuit_breaker_->RealTimeCircuitBreak(test_arg->gauge_,
                                                                       test_arg->test_->circuit_breaker_status_);
  }
  delete test_arg;
  return nullptr;
}

// 多线程错误数计算
TEST_F(ErrorRateCircuitBreakerTest, MultiThreadErrorCount) {
  int thread_num = 5;
  int report_time = 100;
  delete default_config_;
  std::string err_msg;
  // 会自动修复配置
  std::string content = "requestCountAfterHalfOpen:\n  " + std::to_string(thread_num * report_time) +
                        "\nsuccessCountAfterHalfOpen:\n  " + std::to_string(thread_num * report_time + 10) +
                        "\nsleepWindow:\n  " + std::to_string(constants::kHalfOpenSleepWindowDefault * 10);
  default_config_ = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(default_config_ != nullptr && err_msg.empty());
  error_rate_circuit_breaker_->Init(default_config_, context_);
  std::vector<pthread_t> thread_id_list;
  pthread_t tid;
  // 多线程并发上报失败
  instance_gauge_.call_ret_status = kCallRetTimeout;
  for (int i = 0; i < thread_num; i++) {
    ErrorRateRealTimeArg *arg = new ErrorRateRealTimeArg(this, instance_gauge_, report_time, 3);
    pthread_create(&tid, nullptr, MultiThreadRealTimeErrorRate, arg);
    thread_id_list.push_back(tid);
  }
  for (size_t i = 0; i < thread_id_list.size(); i++) {
    pthread_join(thread_id_list[i], nullptr);
  }
  thread_id_list.clear();
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ErrorRateStatus *error_rate_status =
      error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_gauge_.instance_id);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerOpen);

  // 超时半开
  TestUtils::FakeNowIncrement(constants::kHalfOpenSleepWindowDefault * 10);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerHalfOpen);

  // 多线程并发上报成功
  instance_gauge_.call_ret_status = kCallRetOk;
  for (int i = 0; i < thread_num; i++) {
    ErrorRateRealTimeArg *arg = new ErrorRateRealTimeArg(this, instance_gauge_, report_time, 1);
    pthread_create(&tid, nullptr, MultiThreadRealTimeErrorRate, arg);
    thread_id_list.push_back(tid);
  }
  EXPECT_EQ(thread_id_list.size(), thread_num);
  for (size_t i = 0; i < thread_id_list.size(); i++) {
    pthread_join(thread_id_list[i], nullptr);
  }
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerClose);
}

TEST_F(ErrorRateCircuitBreakerTest, TestMetricExpire) {
  error_rate_circuit_breaker_->Init(default_config_, context_);
  error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  auto error_rate_status = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_gauge_.instance_id);
  error_rate_status->status = kCircuitBreakerOpen;
  TestUtils::FakeNowIncrement(constants::kMetricExpiredTimeDefault - 1);

  InstanceExistChecker not_exist = [](const std::string &) { return false; };
  InstanceExistChecker exist = [](const std::string &) { return true; };

  // 未过期
  error_rate_circuit_breaker_->CleanStatus(circuit_breaker_status_, not_exist);
  ASSERT_EQ(error_rate_status->status, kCircuitBreakerOpen);

  TestUtils::FakeNowIncrement(1);
  // 过期，但实例还存在，不清理
  error_rate_circuit_breaker_->CleanStatus(circuit_breaker_status_, exist);
  auto old_error_rate_status = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_gauge_.instance_id);
  ASSERT_EQ(old_error_rate_status->status, kCircuitBreakerOpen);

  // 过期，实例不存在，清理
  TestUtils::FakeNowIncrement(constants::kMetricExpiredTimeDefault);
  error_rate_circuit_breaker_->CleanStatus(circuit_breaker_status_, not_exist);
  auto new_error_rate_status = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_gauge_.instance_id);
  ASSERT_EQ(new_error_rate_status->status, kCircuitBreakerClose);
}

}  // namespace polaris
