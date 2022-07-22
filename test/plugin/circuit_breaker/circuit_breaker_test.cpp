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

#include "plugin/circuit_breaker/chain.h"

#include <gtest/gtest.h>

#include <string>

#include "mock/mock_local_registry.h"
#include "model/constants.h"
#include "plugin/circuit_breaker/error_count.h"
#include "plugin/health_checker/health_checker.h"
#include "test_context.h"
#include "test_utils.h"
#include "utils/utils.h"

namespace polaris {

// ===============================================================
// 熔断链测试
class CircuitBreakerChainDataTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    TestUtils::SetUpFakeTime();
    service_key_.namespace_ = "test_service_namespace";
    service_key_.name_ = "test_service_name";
    mock_local_registry_ = new MockLocalRegistry();
    for (int i = 1; i < 3; i++) {
      CircuitBreakerPluginData plugin_data = {"plugin_" + std::to_string(i), i};
      chain_data_.AppendPluginData(plugin_data);
    }
  }

  virtual void TearDown() {
    TestUtils::TearDownFakeTime();
    if (mock_local_registry_ != nullptr) delete mock_local_registry_;
    TestUtils::TearDownFakeTime();
  }

 protected:
  ServiceKey service_key_;
  MockLocalRegistry *mock_local_registry_;
  CircuitBreakerChainData chain_data_;
};

TEST_F(CircuitBreakerChainDataTest, TestChainDataTranslateStatus) {
  EXPECT_CALL(*mock_local_registry_, UpdateCircuitBreakerData(::testing::_, testing::_)).Times(::testing::Exactly(0));
  CircuitChangeRecord *record = nullptr;
  std::string instance_id = "instance_id";
  record = chain_data_.TranslateStatus(1, instance_id, kCircuitBreakerOpen, kCircuitBreakerHalfOpen);
  chain_data_.CheckAndSyncToLocalRegistry(mock_local_registry_, service_key_);
  ASSERT_TRUE(record == nullptr);
  record = chain_data_.TranslateStatus(1, instance_id, kCircuitBreakerOpen, kCircuitBreakerClose);
  chain_data_.CheckAndSyncToLocalRegistry(mock_local_registry_, service_key_);
  ASSERT_TRUE(record == nullptr);

  EXPECT_CALL(*mock_local_registry_, UpdateCircuitBreakerData(::testing::_, testing::_))
      .WillOnce(::testing::Return(kReturnOk));
  record = chain_data_.TranslateStatus(1, instance_id, kCircuitBreakerClose, kCircuitBreakerOpen);
  chain_data_.CheckAndSyncToLocalRegistry(mock_local_registry_, service_key_);
  ASSERT_TRUE(record != nullptr);
  ASSERT_EQ(record->change_seq_, 1);
  ASSERT_EQ(record->reason_, "plugin_1");
  ASSERT_EQ(record->from_, kCircuitBreakerClose);
  ASSERT_EQ(record->to_, kCircuitBreakerOpen);
  delete record;

  record = chain_data_.TranslateStatus(2, instance_id, kCircuitBreakerOpen, kCircuitBreakerHalfOpen);
  chain_data_.CheckAndSyncToLocalRegistry(mock_local_registry_, service_key_);
  ASSERT_TRUE(record == nullptr);
  record = chain_data_.TranslateStatus(1, instance_id, kCircuitBreakerOpen, kCircuitBreakerOpen);
  chain_data_.CheckAndSyncToLocalRegistry(mock_local_registry_, service_key_);
  ASSERT_TRUE(record == nullptr);

  EXPECT_CALL(*mock_local_registry_, UpdateCircuitBreakerData(::testing::_, testing::_))
      .WillOnce(::testing::Return(kReturnOk));
  record = chain_data_.TranslateStatus(1, instance_id, kCircuitBreakerOpen, kCircuitBreakerHalfOpen);
  chain_data_.CheckAndSyncToLocalRegistry(mock_local_registry_, service_key_);
  ASSERT_TRUE(record != nullptr);
  ASSERT_EQ(record->change_seq_, 2);
  ASSERT_EQ(record->reason_, "plugin_1");
  ASSERT_EQ(record->from_, kCircuitBreakerOpen);
  ASSERT_EQ(record->to_, kCircuitBreakerHalfOpen);
  delete record;

  EXPECT_CALL(*mock_local_registry_, UpdateCircuitBreakerData(::testing::_, testing::_))
      .WillOnce(::testing::Return(kReturnOk));
  record = chain_data_.TranslateStatus(1, instance_id, kCircuitBreakerHalfOpen, kCircuitBreakerClose);
  chain_data_.CheckAndSyncToLocalRegistry(mock_local_registry_, service_key_);
  ASSERT_TRUE(record != nullptr);
  ASSERT_EQ(record->change_seq_, 3);
  ASSERT_EQ(record->reason_, "plugin_1");
  ASSERT_EQ(record->from_, kCircuitBreakerHalfOpen);
  ASSERT_EQ(record->to_, kCircuitBreakerClose);
  delete record;
}

class CircuitBreakerChainTest : public ::testing::Test {
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
    chain_ = new CircuitBreakerChain(service_key_);
    ReturnCode ret = chain_->Init(default_config_, context_, HealthCheckerConfig::kChainWhenNever);
    ASSERT_EQ(ret, kReturnOk);
    instance_exist_checker_ = [](const std::string &) { return false; };
  }

  virtual void TearDown() {
    if (default_config_ != nullptr) delete default_config_;
    if (chain_ != nullptr) delete chain_;
    TestUtils::TearDownFakeTime();
    if (context_ != nullptr) delete context_;
  }

 protected:
  Config *default_config_;
  ServiceKey service_key_;
  Context *context_;
  MockLocalRegistry *mock_local_registry_;
  CircuitBreakerChain *chain_;
  InstanceExistChecker instance_exist_checker_;
};

TEST_F(CircuitBreakerChainTest, TestCreateCircuitBreakerChain) {
  ASSERT_EQ(chain_->GetCircuitBreakers().size(), 2);
  EXPECT_CALL(*mock_local_registry_, UpdateCircuitBreakerData(::testing::_, testing::_)).Times(::testing::Exactly(0));
  ReturnCode ret;
  for (int i = 0; i < 100; i++) {
    TestUtils::FakeNowIncrement(1000);
    ret = chain_->TimingCircuitBreak(instance_exist_checker_);
    ASSERT_EQ(ret, kReturnOk);
  }

  // 配置不存在的插件名报错
  delete default_config_;
  std::string err_msg, content =
                           "enable:\n  true\n"
                           "chain:\n"
                           "  - errorRate\n"
                           "  - errorPlugin\n"
                           "  - errorCount";
  default_config_ = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(default_config_ != nullptr && err_msg.empty());
  CircuitBreakerChain *chain_err_plugin = new CircuitBreakerChain(service_key_);
  ret = chain_err_plugin->Init(default_config_, context_, HealthCheckerConfig::kChainWhenNever);
  ASSERT_EQ(ret, kReturnPluginError);
  delete chain_err_plugin;
}

TEST_F(CircuitBreakerChainTest, TestCircuitBreakerStatusChange) {
  ASSERT_EQ(chain_->GetCircuitBreakers().size(), 2);
  EXPECT_CALL(*mock_local_registry_, UpdateCircuitBreakerData(::testing::_, testing::_))
      .WillOnce(::testing::Return(kReturnOk));

  ReturnCode ret;
  InstanceGauge gauge;
  gauge.instance_id = "instance_id";
  gauge.call_ret_status = kCallRetError;
  for (int i = 0; i < constants::kContinuousErrorThresholdDefault; i++) {
    TestUtils::FakeNowIncrement(1000);
    ret = chain_->RealTimeCircuitBreak(gauge);
    ASSERT_EQ(ret, kReturnOk);
  }
  ErrorCountCircuitBreaker *error_count_circuit_breaker =
      dynamic_cast<ErrorCountCircuitBreaker *>(chain_->GetCircuitBreakers()[0]);
  ASSERT_TRUE(error_count_circuit_breaker != nullptr);
  ErrorCountStatus *error_count_status = error_count_circuit_breaker->GetOrCreateErrorCountStatus(gauge.instance_id);
  ASSERT_EQ(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_count_status->status, kCircuitBreakerOpen);

  TestUtils::FakeNowIncrement(constants::kHalfOpenSleepWindowDefault);
  EXPECT_CALL(*mock_local_registry_, UpdateCircuitBreakerData(::testing::_, testing::_))
      .WillOnce(::testing::Return(kReturnOk));
  ret = chain_->TimingCircuitBreak(instance_exist_checker_);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(error_count_status->last_update_time, Time::GetCoarseSteadyTimeMs());
  ASSERT_EQ(error_count_status->status, kCircuitBreakerHalfOpen);
}

TEST_F(CircuitBreakerChainTest, TestDisableCircuitBreakerChain) {
  if (chain_ != nullptr) {
    delete chain_;
    chain_ = nullptr;
  }
  delete default_config_;
  std::string err_msg, content = "enable:\n  false";
  default_config_ = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(default_config_ != nullptr && err_msg.empty());
  CircuitBreakerChain *disable_chain = new CircuitBreakerChain(service_key_);
  ReturnCode ret = disable_chain->Init(default_config_, context_, HealthCheckerConfig::kChainWhenNever);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(disable_chain->GetCircuitBreakers().size(), 0);

  // 不会更新状态
  EXPECT_CALL(*mock_local_registry_, UpdateCircuitBreakerData(::testing::_, testing::_)).Times(::testing::Exactly(0));

  InstanceGauge gauge;
  gauge.instance_id = "instance_id";
  gauge.call_ret_status = kCallRetError;
  for (int i = 0; i < constants::kContinuousErrorThresholdDefault; i++) {
    TestUtils::FakeNowIncrement(1000);
    ret = disable_chain->RealTimeCircuitBreak(gauge);
    ASSERT_EQ(ret, kReturnOk);
  }

  TestUtils::FakeNowIncrement(constants::kHalfOpenSleepWindowDefault);
  ret = disable_chain->TimingCircuitBreak(instance_exist_checker_);
  ASSERT_EQ(ret, kReturnOk);
  delete disable_chain;
}

}  // namespace polaris
