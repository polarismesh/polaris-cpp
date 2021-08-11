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
    context_ = TestContext::CreateContext(kLimitContext);
    TestUtils::SetUpFakeTime();
    ASSERT_TRUE(context_ != NULL);
    service_key_.namespace_ = "test_namespace";
    service_key_.name_      = "test_name";
    quota_manager_          = NULL;
  }

  virtual void TearDown() {
    if (context_ != NULL) {
      delete context_;
      context_ = NULL;
    }
    if (quota_manager_ != NULL) {
      delete quota_manager_;
      quota_manager_ = NULL;
    }
    TestUtils::TearDownFakeTime();
  }

protected:
  void CreateQuotaManager(bool quota_enable) {
    std::string content;
    if (quota_enable) {
      content =
          "enable:\n"
          "  true\n"
          "mode:\n"
          "  local";
    } else {
      content = "enable:\n  false";
    }
    std::string err_msg;
    Config *config = Config::CreateFromString(content, err_msg);
    ASSERT_TRUE(config != NULL && err_msg.empty());
    quota_manager_ = new QuotaManager();
    ASSERT_EQ(quota_manager_->Init(context_, config), kReturnOk);
    delete config;
  }

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
    QuotaResponse *resp = NULL;
    QuotaInfo quota_info;
    ASSERT_EQ(quota_manager_->GetQuota(req, quota_info, resp), kReturnOk);
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
    ASSERT_EQ(quota_manager_->PrepareQuotaInfo(req, &quota_info), kReturnTimeout);
  }
}

TEST_F(QuotaManagerTest, GetQuotaWithRule) {
  CreateQuotaManager(true);
  MockLocalRegistry *mock_local_registry = TestContext::SetupMockLocalRegistry(context_);
  ASSERT_TRUE(mock_local_registry != NULL);
  v1::DiscoverResponse response;
  FakeServer::CreateServiceRateLimit(response, service_key_, 20);
  ServiceData *service_rate_limit = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  mock_local_registry->service_data_list_.push_back(service_rate_limit);
  for (int i = 0; i < 50; ++i) {
    std::vector<ReturnCode> return_code_list;
    return_code_list.push_back(kReturnOk);
    mock_local_registry->ExpectReturnData(return_code_list, service_key_);
    QuotaRequest req;
    req.SetServiceNamespace(service_key_.namespace_);
    req.SetServiceName(service_key_.name_);
    req.SetTimeout(1000);
    std::map<std::string, std::string> lables;
    lables["label"]  = "value";
    lables["label2"] = "value2";
    req.SetLabels(lables);
    std::map<std::string, std::string> subset;
    subset["subset"] = "value";
    req.SetSubset(subset);
    QuotaInfo quota_info;
    ASSERT_EQ(quota_manager_->PrepareQuotaInfo(req, &quota_info), kReturnOk);
    if (i < 40) {
      ASSERT_EQ(quota_info.GetSericeRateLimitRule()->GetLabelKeys().size(), 1);
      QuotaResponse *resp = NULL;
      ASSERT_EQ(quota_manager_->GetQuota(req, quota_info, resp), kReturnOk);
      ASSERT_TRUE(resp != NULL);
      ASSERT_EQ(resp->GetResultCode(), i < 20 ? kQuotaResultOk : kQuotaResultLimited)
          << i << " " << resp->GetQuotaResultInfo().all_quota_ << " "
          << resp->GetQuotaResultInfo().duration_ << " " << resp->GetQuotaResultInfo().left_quota_
          << " " << resp->GetQuotaResultInfo().is_degrade_;
      delete resp;
    } else {
      ASSERT_EQ(quota_manager_->InitWindow(req, quota_info), kReturnOk);
    }
  }
  ASSERT_EQ(service_rate_limit->DecrementAndGetRef(), 1);  // window里面会引用
}

TEST_F(QuotaManagerTest, PrepareQuotaInfo) {
  delete context_;
  context_ = TestContext::CreateContext();
  ASSERT_TRUE(context_ != NULL);
  CreateQuotaManager(true);

  MockLocalRegistry *mock_local_registry = TestContext::SetupMockLocalRegistry(context_);
  ASSERT_TRUE(mock_local_registry != NULL);
  v1::DiscoverResponse response;
  FakeServer::CreateServiceRateLimit(response, service_key_, 20);
  ServiceData *service_rate_limit = ServiceData::CreateFromPb(&response, kDataNotFound);
  mock_local_registry->service_data_list_.push_back(service_rate_limit);
  for (int i = 0; i < 4; ++i) {
    std::vector<ReturnCode> return_code_list;
    return_code_list.push_back(i % 2 == 0 ? kReturnOk : kReturnNotInit);
    mock_local_registry->ExpectReturnData(return_code_list);
    if (i % 2 != 0) {
      mock_local_registry->ExpectReturnNotify(1);
    }
    QuotaRequest req;
    req.SetServiceNamespace(service_key_.namespace_);
    req.SetServiceName(service_key_.name_);
    req.SetTimeout(100);
    QuotaInfo quota_info;
    ASSERT_EQ(quota_manager_->PrepareQuotaInfo(req, &quota_info),
              i % 2 == 0 ? kReturnServiceNotFound : kReturnTimeout);
    mock_local_registry->DeleteNotify();
  }
  ASSERT_EQ(service_rate_limit->DecrementAndGetRef(), 0);
}

}  // namespace polaris
