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

#include "quota/quota_manager.h"

#include <gtest/gtest.h>

#include "mock/fake_server_response.h"
#include "polaris/limit.h"
#include "quota/quota_model.h"
#include "test_context.h"
#include "test_utils.h"

namespace polaris {

class QuotaManagerTest : public ::testing::Test {
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    ASSERT_TRUE(context_ != nullptr);
    service_key_.namespace_ = "test_namespace";
    service_key_.name_ = "test_name";
    quota_manager_ = nullptr;
  }

  virtual void TearDown() {
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
    if (quota_manager_ != nullptr) {
      delete quota_manager_;
      quota_manager_ = nullptr;
    }
  }

 protected:
  void CreateQuotaManager(bool quota_enable) {
    std::string content;
    if (quota_enable) {
      content = "enable:\n  true";
    } else {
      content = "enable:\n  false";
    }
    std::string err_msg;
    Config *config = Config::CreateFromString(content, err_msg);
    ASSERT_TRUE(config != nullptr && err_msg.empty());
    quota_manager_ = new QuotaManager();
    ASSERT_EQ(quota_manager_->Init(context_, config), kReturnOk);
    delete config;
  }

  QuotaResultCode CheckGetQuota(MockLocalRegistry *mock_local_registry, ServiceData *service_rate_limit,
                                const std::map<std::string, std::string> &request_labels, int request_count,
                                QuotaResultInfo &result_info);

 protected:
  Context *context_;
  QuotaManager *quota_manager_;
  ServiceKey service_key_;
};

TEST_F(QuotaManagerTest, GetQuotaWithQuotaDisable) {
  CreateQuotaManager(false);  //不开启限流
  for (int i = 0; i < 100; ++i) {
    QuotaRequest req = QuotaRequest();
    req.SetServiceNamespace(service_key_.namespace_);
    req.SetServiceName(service_key_.name_);
    QuotaResponse *resp = nullptr;
    QuotaInfo quota_info;
    ASSERT_EQ(quota_manager_->GetQuota(req.GetImpl(), quota_info, resp), kReturnOk);
    ASSERT_EQ(resp->GetResultCode(), kQuotaResultOk);
    delete resp;
  }
}

TEST_F(QuotaManagerTest, GetQuotaRuleTimeout) {
  CreateQuotaManager(true);
  for (int i = 0; i < 10; ++i) {
    QuotaRequest req = QuotaRequest();
    req.SetServiceNamespace(service_key_.namespace_);
    req.SetServiceName(service_key_.name_);
    req.SetTimeout(1);
    QuotaInfo quota_info;
    ASSERT_EQ(quota_manager_->PrepareQuotaInfo(req.GetImpl(), &quota_info), kReturnTimeout);
  }
}

TEST_F(QuotaManagerTest, GetQuotaWithRule) {
  CreateQuotaManager(true);
  MockLocalRegistry *mock_local_registry = TestContext::SetupMockLocalRegistry(context_);
  ASSERT_TRUE(mock_local_registry != nullptr);
  v1::DiscoverResponse response;
  FakeServer::CreateServiceRateLimit(response, service_key_, 20);
  ServiceData *service_rate_limit = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  for (int i = 0; i < 50; ++i) {
    std::vector<ReturnCode> return_code_list;
    return_code_list.push_back(kReturnOk);
    mock_local_registry->ExpectReturnData(return_code_list, service_key_);
    mock_local_registry->service_data_list_.push_back(service_rate_limit);
    QuotaRequest req;
    req.SetServiceNamespace(service_key_.namespace_);
    req.SetServiceName(service_key_.name_);
    req.SetTimeout(1000);
    std::map<std::string, std::string> labels;
    labels["label"] = "value";
    labels["label2"] = "value2";
    req.SetLabels(labels);
    req.SetMethod("value");
    QuotaInfo quota_info;
    ASSERT_EQ(quota_manager_->PrepareQuotaInfo(req.GetImpl(), &quota_info), kReturnOk);
    if (i < 40) {
      ASSERT_EQ(quota_info.GetServiceRateLimitRule()->GetLabelKeys().size(), 1);
      QuotaResponse *resp = nullptr;
      ASSERT_EQ(quota_manager_->GetQuota(req.GetImpl(), quota_info, resp), kReturnOk);
      ASSERT_TRUE(resp != nullptr);
      ASSERT_EQ(resp->GetResultCode(), i < 20 ? kQuotaResultOk : kQuotaResultLimited)
          << i << " " << resp->GetQuotaResultInfo().all_quota_ << " " << resp->GetQuotaResultInfo().duration_ << " "
          << resp->GetQuotaResultInfo().left_quota_ << " " << resp->GetQuotaResultInfo().is_degrade_;
      delete resp;
    } else {
      ASSERT_EQ(quota_manager_->InitWindow(req.GetImpl(), quota_info), kReturnOk);
    }
  }
  ASSERT_EQ(service_rate_limit->DecrementAndGetRef(), 1);  // window里面会引用
}

TEST_F(QuotaManagerTest, PrepareQuotaInfo) {
  CreateQuotaManager(true);

  MockLocalRegistry *mock_local_registry = TestContext::SetupMockLocalRegistry(context_);
  ASSERT_TRUE(mock_local_registry != nullptr);
  v1::DiscoverResponse response;
  FakeServer::CreateServiceRateLimit(response, service_key_, 20);
  ServiceData *service_rate_limit = ServiceData::CreateFromPb(&response, kDataNotFound);
  for (int i = 0; i < 4; ++i) {
    std::vector<ReturnCode> return_code_list;
    return_code_list.push_back(i % 2 == 0 ? kReturnOk : kReturnNotInit);
    mock_local_registry->ExpectReturnData(return_code_list);
    if (i % 2 == 0) {
      mock_local_registry->service_data_list_.push_back(service_rate_limit);
    } else {
      mock_local_registry->service_data_list_.push_back(nullptr);
      mock_local_registry->ExpectReturnNotify(1);
    }
    QuotaRequest req;
    req.SetServiceNamespace(service_key_.namespace_);
    req.SetServiceName(service_key_.name_);
    req.SetTimeout(100);
    QuotaInfo quota_info;
    ASSERT_EQ(quota_manager_->PrepareQuotaInfo(req.GetImpl(), &quota_info),
              i % 2 == 0 ? kReturnServiceNotFound : kReturnTimeout);
    mock_local_registry->DeleteNotify();
  }
  ASSERT_EQ(service_rate_limit->DecrementAndGetRef(), 0);
}

QuotaResultCode QuotaManagerTest::CheckGetQuota(MockLocalRegistry *mock_local_registry, ServiceData *service_rate_limit,
                                                const std::map<std::string, std::string> &request_labels,
                                                int request_count, QuotaResultInfo &result_info) {
  QuotaResultCode result_code = kQuotaResultOk;
  for (int i = 0; i < request_count; ++i) {
    std::vector<ReturnCode> return_code_list;
    return_code_list.push_back(kReturnOk);
    mock_local_registry->ExpectReturnData(return_code_list, service_key_);
    EXPECT_EQ(mock_local_registry->service_data_list_.size(), 0);
    mock_local_registry->service_data_list_.push_back(service_rate_limit);
    QuotaRequest request;
    request.SetServiceNamespace(service_key_.namespace_);
    request.SetServiceName(service_key_.name_);
    request.SetLabels(request_labels);
    request.SetTimeout(10);
    QuotaInfo quota_info;
    EXPECT_EQ(quota_manager_->PrepareQuotaInfo(request.GetImpl(), &quota_info), kReturnOk);
    QuotaResponse *quota_resp = nullptr;
    EXPECT_EQ(quota_manager_->GetQuota(request.GetImpl(), quota_info, quota_resp), kReturnOk);
    EXPECT_TRUE(quota_resp != nullptr);
    result_code = quota_resp->GetResultCode();
    result_info = quota_resp->GetQuotaResultInfo();
    delete quota_resp;
  }
  return result_code;
}

TEST_F(QuotaManagerTest, TestWindowExpired) {
  CreateQuotaManager(true);
  MockLocalRegistry *mock_local_registry = TestContext::SetupMockLocalRegistry(context_);
  ASSERT_TRUE(mock_local_registry != nullptr);
  // 两条限流规则
  v1::DiscoverResponse response;
  response.mutable_code()->set_value(v1::ExecuteSuccess);
  response.set_type(v1::DiscoverResponse::RATE_LIMIT);
  FakeServer::SetService(response, service_key_);
  v1::RateLimit *rate_limit = response.mutable_ratelimit();
  rate_limit->mutable_revision()->set_value("version_one");
  for (int i = 1; i <= 2; ++i) {
    v1::Rule *rule = rate_limit->add_rules();
    rule->mutable_id()->set_value("rule" + std::to_string(i));
    rule->mutable_namespace_()->set_value(service_key_.namespace_);
    rule->mutable_service()->set_value(service_key_.name_);
    rule->set_type(v1::Rule::GLOBAL);
    v1::MatchString match_string;
    match_string.set_type(v1::MatchString::REGEX);
    match_string.mutable_value()->set_value(".*");
    (*rule->mutable_labels())["key" + std::to_string(i)] = match_string;
    v1::Amount *amount = rule->add_amounts();
    amount->mutable_maxamount()->set_value(100);                 // 限流请求数为100
    amount->mutable_validduration()->set_seconds(24 * 60 * 60);  // 限流周期为一天
    rule->mutable_revision()->set_value("version" + std::to_string(i));
  }

  TestUtils::SetUpFakeTime();

  ServiceData *service_rate_limit = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  ASSERT_TRUE(service_rate_limit != nullptr);
  std::map<std::string, std::string> labels;
  QuotaResultInfo result_info;
  // 第1个限流窗口匹配第1条规则，触发限流
  labels["key1"] = "value1";
  ASSERT_EQ(CheckGetQuota(mock_local_registry, service_rate_limit, labels, 101, result_info), kQuotaResultLimited);
  ASSERT_EQ(result_info.left_quota_, 0);

  // 第2个限流窗口匹配第1条规则，不触发限流
  labels.clear();
  labels["key1"] = "value2";
  ASSERT_EQ(CheckGetQuota(mock_local_registry, service_rate_limit, labels, 99, result_info), kQuotaResultOk);
  ASSERT_EQ(result_info.left_quota_, 1);

  // 第3个限流窗口匹配第2条规则，触发限流
  labels.clear();
  labels["key2"] = "value1";
  ASSERT_EQ(CheckGetQuota(mock_local_registry, service_rate_limit, labels, 101, result_info), kQuotaResultLimited);
  ASSERT_EQ(result_info.left_quota_, 0);

  service_rate_limit->DecrementRef();
  // 屏蔽第2条规则
  rate_limit->mutable_rules(1)->mutable_disable()->set_value(true);
  service_rate_limit = ServiceData::CreateFromPb(&response, kDataIsSyncing);

  // 执行淘汰逻辑
  std::vector<ReturnCode> return_code_list;  // 两个窗口淘汰判断会获取2次规则数据
  return_code_list.push_back(kReturnOk);
  return_code_list.push_back(kReturnOk);
  mock_local_registry->ExpectReturnData(return_code_list, service_key_);
  mock_local_registry->service_data_list_.push_back(service_rate_limit);
  mock_local_registry->service_data_list_.push_back(service_rate_limit);
  TestUtils::FakeNowIncrement(61 * 1000);  // 61s触发淘汰
  sleep(2);                                // 2s等待过期检查任务执行

  // 第1个窗口，到了过期时间，已经限流，不淘汰，限流
  labels.clear();
  labels["key1"] = "value1";
  ASSERT_EQ(CheckGetQuota(mock_local_registry, service_rate_limit, labels, 1, result_info), kQuotaResultLimited);
  ASSERT_EQ(result_info.left_quota_, 0);

  // 第2个限流窗口，到了过期时间，未被限流，淘汰后，不限流
  labels.clear();
  labels["key1"] = "value2";
  ASSERT_EQ(CheckGetQuota(mock_local_registry, service_rate_limit, labels, 99, result_info), kQuotaResultOk);
  ASSERT_EQ(result_info.left_quota_, 1);

  // 第3个限流窗口，已经限流，但规则失效，淘汰后，不限流
  labels.clear();
  labels["key2"] = "value1";
  ASSERT_EQ(CheckGetQuota(mock_local_registry, service_rate_limit, labels, 101, result_info), kQuotaResultOk);
  ASSERT_EQ(result_info.duration_, 0);

  service_rate_limit->DecrementRef();
  TestUtils::TearDownFakeTime();
}

}  // namespace polaris
