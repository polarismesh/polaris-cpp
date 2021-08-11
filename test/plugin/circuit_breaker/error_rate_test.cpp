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

#include <string>

#include "mock/mock_local_registry.h"
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
    context_     = TestContext::CreateContext();
    service_key_ = context_->GetContextImpl()->GetDiscoverService().service_;
    TestUtils::SetUpFakeTime();
    default_config_ = Config::CreateEmptyConfig();
    ASSERT_TRUE(default_config_ != NULL);
    error_rate_circuit_breaker_ = new ErrorRateCircuitBreaker();
    error_rate_circuit_breaker_->Init(default_config_, NULL);
    instance_gauge_.instance_id     = "uuid-2";
    instance_gauge_.call_daley      = 100;
    instance_gauge_.call_ret_code   = 42;
    instance_gauge_.call_ret_status = kCallRetOk;
    chain_data_                     = new CircuitBreakerChainData();
    CircuitBreakerPluginData plugin_data;
    plugin_data.plugin_name             = "errorRate";
    plugin_data.request_after_half_open = CircuitBreakerConfig::kRequestCountAfterHalfOpenDefault;
    chain_data_->AppendPluginData(plugin_data);
    circuit_breaker_status_ = new InstancesCircuitBreakerStatusImpl(
        chain_data_, 1, service_key_, context_->GetContextImpl()->GetServiceRecord(), true);
    default_bucket_time_ =
        ceilf(static_cast<float>(CircuitBreakerConfig::kMetricStatTimeWindowDefault) /
              CircuitBreakerConfig::kMetricNumBucketsDefault);
  }

  virtual void TearDown() {
    if (error_rate_circuit_breaker_ != NULL) delete error_rate_circuit_breaker_;
    if (circuit_breaker_status_ != NULL) delete circuit_breaker_status_;
    if (chain_data_ != NULL) delete chain_data_;
    if (default_config_ != NULL) delete default_config_;
    TestUtils::TearDownFakeTime();
    if (context_ != NULL) delete context_;
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
  InstancesCircuitBreakerStatusImpl *circuit_breaker_status_;
};

// 单线程创建或获取统计状态
TEST_F(ErrorRateCircuitBreakerTest, OneThreadGetOrCreateStatus) {
  // 首次获取
  std::string instance_id = "instance";
  ErrorRateStatus &status = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_id, 0);
  ASSERT_EQ(status.status, kCircuitBreakerClose);
  ASSERT_TRUE(status.buckets != NULL);
  ASSERT_EQ(status.last_update_time, 0);
  ASSERT_EQ(status.last_access_time, 0);

  // 修改数据
  status.buckets[0].bucket_time = 1;
  status.buckets[0].error_count = 2;
  status.buckets[0].total_count = 3;
  status.last_access_time       = 1;
  status.last_update_time       = 2;

  // 再次获取，是同一个状态对象
  ErrorRateStatus &status2 =
      error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_id, 3);
  ASSERT_EQ(&status, &status2);
  ASSERT_EQ(status2.buckets[0].bucket_time, 1);
  ASSERT_EQ(status2.buckets[0].error_count, 2);
  ASSERT_EQ(status2.buckets[0].total_count, 3);
  ASSERT_EQ(status2.last_access_time, 3);
  ASSERT_EQ(status2.last_update_time, 2);
}

// 每个线程创建不同的实例
void *ErrorRateCircuitBreakerTest::MultiThreadGetOrCreateErrorRateMap(void *arg) {
  ErrorRateCircuitBreakerTest *test = static_cast<ErrorRateCircuitBreakerTest *>(arg);
  pthread_t tid                     = pthread_self();
  std::stringstream instance_id;
  instance_id << "instance_" << tid;
  for (int i = 0; i < kTestMultiThreadGetOrCreateTime; ++i) {
    ErrorRateStatus &status =
        test->error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_id.str(), i);
    EXPECT_EQ(status.last_access_time, i);
    EXPECT_EQ(status.last_update_time, i);

    // 单线程修改数据
    status.last_access_time++;
    status.last_update_time++;
  }
  return NULL;
}

// 多线程创建或获取统计状态
TEST_F(ErrorRateCircuitBreakerTest, MultiThreadGetOrCreateStatusMap) {
  int thread_num = 5;
  std::vector<pthread_t> thread_id_list;
  pthread_t tid;
  // 多线程并发查询和修改Map中的数据，每给线程只修改自己对应的实例
  for (int i = 0; i < thread_num; i++) {
    pthread_create(&tid, NULL, MultiThreadGetOrCreateErrorRateMap, this);
    thread_id_list.push_back(tid);
  }
  EXPECT_EQ(thread_id_list.size(), thread_num);
  for (size_t i = 0; i < thread_id_list.size(); i++) {
    pthread_join(thread_id_list[i], NULL);
  }
  for (int i = 0; i < thread_num; ++i) {
    std::stringstream instance_id;
    instance_id << "instance_" << thread_id_list[i];
    ErrorRateStatus &status = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(
        instance_id.str(), kTestMultiThreadGetOrCreateTime);
    ASSERT_EQ(status.last_access_time, kTestMultiThreadGetOrCreateTime);
    ASSERT_EQ(status.last_update_time, kTestMultiThreadGetOrCreateTime);
  }
}

// 每个线程创建相同的实例
void *ErrorRateCircuitBreakerTest::MultiThreadGetOrCreateErrorRateInstance(void *arg) {
  ErrorRateCircuitBreakerTest *test = static_cast<ErrorRateCircuitBreakerTest *>(arg);
  for (int i = 0; i < kTestMultiThreadGetOrCreateTime; ++i) {
    std::stringstream instance_id;
    instance_id << "instance_" << i;
    ErrorRateStatus &status = test->error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(
        instance_id.str(), kTestMultiThreadGetOrCreateTime);
    // 并发修改数据
    ATOMIC_INC(&status.last_update_time);
  }
  return NULL;
}

// 多线程创建或获取统计状态
TEST_F(ErrorRateCircuitBreakerTest, MultiThreadGetOrCreateStatusInstance) {
  int thread_num = 5;
  std::vector<pthread_t> thread_id_list;
  pthread_t tid;
  // 多线程并发创建修改相同实例数据
  for (int i = 0; i < thread_num; i++) {
    pthread_create(&tid, NULL, MultiThreadGetOrCreateErrorRateInstance, this);
    thread_id_list.push_back(tid);
  }
  EXPECT_EQ(thread_id_list.size(), thread_num);
  for (size_t i = 0; i < thread_id_list.size(); i++) {
    pthread_join(thread_id_list[i], NULL);
  }
  for (int i = 0; i < kTestMultiThreadGetOrCreateTime; ++i) {
    std::stringstream instance_id;
    instance_id << "instance_" << i;
    ErrorRateStatus &status = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(
        instance_id.str(), kTestMultiThreadGetOrCreateTime + 1);
    ASSERT_EQ(status.last_access_time, kTestMultiThreadGetOrCreateTime + 1);
    ASSERT_EQ(status.last_update_time, thread_num);
  }
}

// 测试数达标
TEST_F(ErrorRateCircuitBreakerTest, TestRequestVolumeThreshold) {
  error_rate_circuit_breaker_->Init(default_config_, NULL);
  // 区间内错误数不达标，但错误率不达标 60s 12bucket 10请求 0.5错误率
  instance_gauge_.call_ret_status    = kCallRetError;
  ErrorRateStatus &error_rate_status = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(
      instance_gauge_.instance_id, Time::GetCurrentTimeMs());
  uint64_t current_time;
  for (int i = 0; i < CircuitBreakerConfig::kRequestVolumeThresholdDefault - 1; ++i) {
    current_time         = Time::GetCurrentTimeMs();
    uint64_t bucket_time = current_time / default_bucket_time_;
    int bucket_index     = bucket_time % CircuitBreakerConfig::kMetricNumBucketsDefault;
    error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
    ASSERT_EQ(error_rate_status.status, kCircuitBreakerClose);
    ASSERT_EQ(error_rate_status.buckets[bucket_index].bucket_time,
              current_time / default_bucket_time_);
    ASSERT_EQ(error_rate_status.buckets[bucket_index].error_count, 1);
    ASSERT_EQ(error_rate_status.buckets[bucket_index].total_count, 1);
    ASSERT_EQ(error_rate_status.last_access_time, current_time);
    TestUtils::FakeNowIncrement(default_bucket_time_);
  }

  // 请求数和失败率都达标
  error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status.status, kCircuitBreakerOpen);
  ASSERT_EQ(error_rate_status.last_update_time, Time::GetCurrentTimeMs());
}

// 单线程错误数计算
TEST_F(ErrorRateCircuitBreakerTest, SingleThreadErrorCount) {
  error_rate_circuit_breaker_->Init(default_config_, NULL);
  ErrorRateStatus &error_rate_status =
      error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(instance_gauge_.instance_id, 0);

  // 错误数很多，但一直不连续，每当差一次达到阈值又成功一次
  instance_gauge_.call_ret_status = kCallRetOk;
  error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  uint64_t request_count = CircuitBreakerConfig::kRequestVolumeThresholdDefault * 2;
  for (uint64_t i = 0; i < request_count; i++) {
    error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
    TestUtils::FakeNowIncrement(1000);
    instance_gauge_.call_ret_status = i % 2 == 0 ? kCallRetOk : kCallRetError;
    error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    ASSERT_EQ(error_rate_status.last_update_time, 0);
    ASSERT_EQ(error_rate_status.last_access_time, Time::GetCurrentTimeMs());
    ASSERT_EQ(error_rate_status.status, kCircuitBreakerClose);
  }
  // 超过默认阈值50%，熔断
  instance_gauge_.call_ret_status = kCallRetError;
  error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status.last_update_time, Time::GetCurrentTimeMs());
  ASSERT_EQ(error_rate_status.status, kCircuitBreakerOpen);

  // 变成打开状态后，过了配置的时间变成半开状态
  TestUtils::FakeNowIncrement(CircuitBreakerConfig::kHalfOpenSleepWindowDefault);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status.last_update_time, Time::GetCurrentTimeMs());
  ASSERT_EQ(error_rate_status.status, kCircuitBreakerHalfOpen);

  // 半开后错误一定次数立刻重新熔断
  instance_gauge_.call_ret_status = kCallRetError;
  request_count                   = CircuitBreakerConfig::kRequestCountAfterHalfOpenDefault -
                  CircuitBreakerConfig::kSuccessCountAfterHalfOpenDefault + 1;
  for (uint64_t i = 0; i < request_count; i++) {
    TestUtils::FakeNowIncrement(100);
    ASSERT_EQ(error_rate_status.status, kCircuitBreakerHalfOpen);
    error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  }
  ASSERT_EQ(error_rate_status.status, kCircuitBreakerOpen);
  ASSERT_EQ(error_rate_status.last_update_time, Time::GetCurrentTimeMs());

  // 重新半开
  TestUtils::FakeNowIncrement(CircuitBreakerConfig::kHalfOpenSleepWindowDefault - 1);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status.status, kCircuitBreakerOpen);
  TestUtils::FakeNowIncrement(1);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status.last_update_time, Time::GetCurrentTimeMs());
  ASSERT_EQ(error_rate_status.status, kCircuitBreakerHalfOpen);

  // 全部放量结束后，成功数不够重新熔断
  for (uint64_t i = 1; i < CircuitBreakerConfig::kRequestCountAfterHalfOpenDefault; i++) {
    TestUtils::FakeNowIncrement(100);
    instance_gauge_.call_ret_status =
        i < CircuitBreakerConfig::kSuccessCountAfterHalfOpenDefault ? kCallRetOk : kCallRetError;
    error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    ASSERT_EQ(error_rate_status.last_access_time, Time::GetCurrentTimeMs());
    ASSERT_EQ(error_rate_status.status, kCircuitBreakerHalfOpen);
  }
  TestUtils::FakeNowIncrement(CircuitBreakerConfig::kHalfOpenSleepWindowDefault);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_LE(error_rate_status.last_update_time, Time::GetCurrentTimeMs());
  ASSERT_EQ(error_rate_status.status, kCircuitBreakerHalfOpen);
  instance_gauge_.call_ret_status = kCallRetError;
  error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status.last_update_time, Time::GetCurrentTimeMs());
  ASSERT_EQ(error_rate_status.status, kCircuitBreakerOpen);

  // 重新半开
  TestUtils::FakeNowIncrement(CircuitBreakerConfig::kHalfOpenSleepWindowDefault);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status.last_update_time, Time::GetCurrentTimeMs());
  ASSERT_EQ(error_rate_status.status, kCircuitBreakerHalfOpen);

  // 成功请求数达到条件，恢复
  instance_gauge_.call_ret_status = kCallRetOk;
  request_count                   = CircuitBreakerConfig::kSuccessCountAfterHalfOpenDefault;
  for (uint64_t i = 0; i < request_count; i++) {
    TestUtils::FakeNowIncrement(100);
    error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
    ASSERT_EQ(error_rate_status.last_access_time, Time::GetCurrentTimeMs());
    ASSERT_EQ(error_rate_status.status, kCircuitBreakerHalfOpen);
  }
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status.last_update_time, Time::GetCurrentTimeMs());
  ASSERT_EQ(error_rate_status.status, kCircuitBreakerClose);
}

struct ErrorRateRealTimeArg {
  ErrorRateRealTimeArg(ErrorRateCircuitBreakerTest *test, InstanceGauge gauge, int report_time,
                       int ret_code_mod)
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
    test_arg->gauge_.call_ret_status =
        i % test_arg->ret_code_mod_ == 0 ? kCallRetOk : kCallRetError;
    test_arg->test_->error_rate_circuit_breaker_->RealTimeCircuitBreak(
        test_arg->gauge_, test_arg->test_->circuit_breaker_status_);
  }
  delete test_arg;
  return NULL;
}

// 多线程错误数计算
TEST_F(ErrorRateCircuitBreakerTest, MultiThreadErrorCount) {
  int thread_num  = 5;
  int report_time = 100;
  delete default_config_;
  std::string err_msg;
  // 会自动修复配置
  std::string content =
      "requestCountAfterHalfOpen:\n  " + StringUtils::TypeToStr<int>(thread_num * report_time) +
      "\nsuccessCountAfterHalfOpen:\n  " +
      StringUtils::TypeToStr<int>(thread_num * report_time + 10) + "\nsleepWindow:\n  " +
      StringUtils::TypeToStr<uint64_t>(CircuitBreakerConfig::kHalfOpenSleepWindowDefault * 10);
  default_config_ = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(default_config_ != NULL && err_msg.empty());
  error_rate_circuit_breaker_->Init(default_config_, NULL);
  std::vector<pthread_t> thread_id_list;
  pthread_t tid;
  // 多线程并发上报失败
  instance_gauge_.call_ret_status = kCallRetTimeout;
  for (int i = 0; i < thread_num; i++) {
    ErrorRateRealTimeArg *arg = new ErrorRateRealTimeArg(this, instance_gauge_, report_time, 3);
    pthread_create(&tid, NULL, MultiThreadRealTimeErrorRate, arg);
    thread_id_list.push_back(tid);
  }
  for (size_t i = 0; i < thread_id_list.size(); i++) {
    pthread_join(thread_id_list[i], NULL);
  }
  thread_id_list.clear();
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ErrorRateStatus &error_rate_status = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(
      instance_gauge_.instance_id, Time::GetCurrentTimeMs());
  ASSERT_EQ(error_rate_status.last_update_time, Time::GetCurrentTimeMs());
  ASSERT_EQ(error_rate_status.status, kCircuitBreakerOpen);

  // 超时半开
  TestUtils::FakeNowIncrement(CircuitBreakerConfig::kHalfOpenSleepWindowDefault * 10);
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status.last_update_time, Time::GetCurrentTimeMs());
  ASSERT_EQ(error_rate_status.status, kCircuitBreakerHalfOpen);

  // 多线程并发上报成功
  instance_gauge_.call_ret_status = kCallRetOk;
  for (int i = 0; i < thread_num; i++) {
    ErrorRateRealTimeArg *arg = new ErrorRateRealTimeArg(this, instance_gauge_, report_time, 1);
    pthread_create(&tid, NULL, MultiThreadRealTimeErrorRate, arg);
    thread_id_list.push_back(tid);
  }
  EXPECT_EQ(thread_id_list.size(), thread_num);
  for (size_t i = 0; i < thread_id_list.size(); i++) {
    pthread_join(thread_id_list[i], NULL);
  }
  error_rate_circuit_breaker_->TimingCircuitBreak(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status.last_update_time, Time::GetCurrentTimeMs());
  ASSERT_EQ(error_rate_status.status, kCircuitBreakerClose);
}

TEST_F(ErrorRateCircuitBreakerTest, TestMetricExpire) {
  error_rate_circuit_breaker_->Init(default_config_, NULL);
  uint64_t create_time = Time::GetCurrentTimeMs();
  error_rate_circuit_breaker_->RealTimeCircuitBreak(instance_gauge_, circuit_breaker_status_);
  ErrorRateStatus &error_rate_status = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(
      instance_gauge_.instance_id, create_time);
  ASSERT_EQ(error_rate_status.last_access_time, create_time);
  TestUtils::FakeNowIncrement(CircuitBreakerConfig::kMetricExpiredTimeDefault - 1);

  // 未过期
  error_rate_circuit_breaker_->CheckAndExpiredMetric(circuit_breaker_status_);
  ASSERT_EQ(error_rate_status.last_access_time, create_time);

  TestUtils::FakeNowIncrement(1);
  // 过期
  error_rate_circuit_breaker_->CheckAndExpiredMetric(circuit_breaker_status_);
  ErrorRateStatus &new_error_rate_status = error_rate_circuit_breaker_->GetOrCreateErrorRateStatus(
      instance_gauge_.instance_id, create_time + 1);
  ASSERT_EQ(new_error_rate_status.last_access_time, create_time + 1);  // 刚创建
}

}  // namespace polaris
