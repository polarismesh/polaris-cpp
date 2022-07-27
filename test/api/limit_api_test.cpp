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

#include "polaris/limit.h"

#include <gtest/gtest.h>

#include "api/limit_api.h"
#include "polaris/consumer.h"
#include "polaris/provider.h"
#include "test_utils.h"
#include "utils/file_utils.h"

namespace polaris {

class LimitApiTest : public ::testing::Test {
 protected:
  virtual void SetUp() { config_ = nullptr; }

  virtual void TearDown() { DeleteConfig(); }

  void CreateConfig() {
    std::string err_msg;
    content_ =
        "global:\n"
        "  serverConnector:\n"
        "    addresses:\n"
        "      - 127.0.0.1:8081";
    config_ = Config::CreateFromString(content_, err_msg);
    ASSERT_TRUE(config_ != nullptr && err_msg.empty()) << err_msg;
  }

  void DeleteConfig() {
    if (config_ != nullptr) {
      delete config_;
      config_ = nullptr;
    }
  }

 protected:
  std::string content_;
  Config *config_;
};

TEST_F(LimitApiTest, TestCreateFromContext) {
  LimitApi *limit_api = LimitApi::Create(nullptr);
  ASSERT_FALSE(limit_api != nullptr);  // Context为NULL无法创建

  this->CreateConfig();
  Context *context = Context::Create(config_);  // share context
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  limit_api = LimitApi::Create(context);
  ASSERT_TRUE(limit_api != nullptr);
  delete limit_api;
  delete context;

  this->CreateConfig();
  context = Context::Create(config_, kShareContextWithoutEngine);
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  limit_api = LimitApi::Create(context);
  ASSERT_FALSE(limit_api != nullptr);  // mode不对无法创建
  delete context;

  this->CreateConfig();
  context = Context::Create(config_, kPrivateContext);
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  limit_api = LimitApi::Create(context);
  ASSERT_FALSE(limit_api != nullptr);
  delete context;

  this->CreateConfig();
  context = Context::Create(config_, kLimitContext);
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  std::string err_msg;
  limit_api = LimitApi::Create(context, err_msg);
  ASSERT_TRUE(limit_api != nullptr);
  ASSERT_TRUE(err_msg.empty());
  ConsumerApi *consumer = ConsumerApi::Create(context);
  ASSERT_TRUE(consumer != nullptr);
  ProviderApi *provider = ProviderApi::Create(context);
  ASSERT_TRUE(provider != nullptr);
  delete provider;
  delete consumer;
  delete limit_api;
}

TEST_F(LimitApiTest, TestCreateFromConfig) {
  Config *config = nullptr;
  LimitApi *limit_api = LimitApi::CreateFromConfig(config);
  ASSERT_FALSE(limit_api != nullptr);  // 配置为null， 无法创建provider api

  std::string err_msg, content =
                           "global:\n"
                           "  serverConnector:\n"
                           "    addresses: []";
  config = Config::CreateFromString(content, err_msg);
  ASSERT_TRUE(config != nullptr && err_msg.empty());
  limit_api = LimitApi::CreateFromConfig(config);
  ASSERT_FALSE(limit_api != nullptr);  // 缺少server地址配置无法创建context，导致无法创建limit api
  delete config;

  this->CreateConfig();
  limit_api = LimitApi::CreateFromConfig(config_);
  this->DeleteConfig();
  ASSERT_TRUE(limit_api != nullptr);  // 正常创建
  delete limit_api;

  this->CreateConfig();
  limit_api = LimitApi::CreateFromConfig(config_, err_msg);
  ASSERT_TRUE(limit_api != nullptr);
  ASSERT_TRUE(err_msg.empty());
  delete limit_api;
}

TEST_F(LimitApiTest, TestCreateFromFile) {
  LimitApi *limit_api = LimitApi::CreateFromFile("not_exist.file");
  ASSERT_FALSE(limit_api != nullptr);  // 从不存在的文件创建失败

  // 创建临时文件
  std::string config_file;
  TestUtils::CreateTempFile(config_file);

  limit_api = LimitApi::CreateFromFile(config_file);
  ASSERT_TRUE(limit_api != nullptr);
  delete limit_api;
  FileUtils::RemoveFile(config_file);

  // 写入配置
  content_ =
      "rateLimiter:\n"
      "  batchInterval: 100ms";
  TestUtils::CreateTempFileWithContent(config_file, content_);
  limit_api = LimitApi::CreateFromFile(config_file);
  ASSERT_TRUE(limit_api != nullptr);  // 创建成功
  delete limit_api;
  FileUtils::RemoveFile(config_file);

  limit_api = LimitApi::CreateWithDefaultFile();
  ASSERT_TRUE(limit_api != nullptr);
  delete limit_api;

  std::string err_msg;
  limit_api = LimitApi::CreateWithDefaultFile(err_msg);
  ASSERT_TRUE(limit_api != nullptr);
  delete limit_api;
}

TEST_F(LimitApiTest, TestCreateFromString) {
  std::string content;
  LimitApi *limit_api = LimitApi::CreateFromString(content);
  ASSERT_TRUE(limit_api != nullptr);  // 空字符串可以创建
  delete limit_api;

  content = "[,,,";
  limit_api = LimitApi::CreateFromString(content);
  ASSERT_FALSE(limit_api != nullptr);  // 错误的字符串无法创建

  std::string err_msg;
  limit_api = LimitApi::CreateFromString(content, err_msg);
  ASSERT_FALSE(limit_api != nullptr);
  ASSERT_FALSE(err_msg.empty());

  content_ =
      "global:\n"
      "  serverConnector:\n"
      "    addresses:\n"
      "      - 127.0.0.1:8081\n"  
      "rateLimiter:\n"
      "  batchInterval: 100ms";
  limit_api = LimitApi::CreateFromString(content_);
  ASSERT_TRUE(limit_api != nullptr);  // 创建成功
  delete limit_api;
}

TEST_F(LimitApiTest, TestCreateFromShareContext) {
  LimitApi *limit_api = LimitApi::Create(nullptr);
  ASSERT_FALSE(limit_api != nullptr);  // Context为NULL无法创建
  std::string err_msg;
  content_ =
      "global:\n"
      "  serverConnector:\n"
      "    addresses:\n"
      "      - 127.0.0.1:8081\n"
      "rateLimiter:\n"
      "  batchInterval: 100ms";
  config_ = Config::CreateFromString(content_, err_msg);
  ASSERT_TRUE(config_ != nullptr && err_msg.empty()) << err_msg;
  Context *context = Context::Create(config_, kShareContext);  // share context
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  delete context;

  content_ =
      "global:\n"
      "  serverConnector:\n"
      "    addresses:\n"
      "      - 127.0.0.1:8081\n"
      "rateLimiter:\n"
      "  rateLimitCluster:\n"
      "    namespace: Polaris\n"
      "    service: polaris.metric.test";
  config_ = Config::CreateFromString(content_, err_msg);
  ASSERT_TRUE(config_ != nullptr && err_msg.empty()) << err_msg;
  context = Context::Create(config_, kShareContext);  // share context
  this->DeleteConfig();
  ASSERT_TRUE(context != nullptr);
  limit_api = LimitApi::Create(context);
  ASSERT_TRUE(limit_api != nullptr);
  delete limit_api;
  delete context;
}

TEST_F(LimitApiTest, FetchRuleTimeout) {
  CreateConfig();
  LimitApi *limit_api = LimitApi::CreateFromConfig(config_);
  ASSERT_TRUE(limit_api != nullptr);
  ServiceKey service_key = {"test", "test.limit.service"};
  std::string json_rule;
  ASSERT_EQ(limit_api->FetchRule(service_key, json_rule), kReturnTimeout);
  ASSERT_EQ(limit_api->FetchRule(service_key, 100, json_rule), kReturnTimeout);

  const std::set<std::string> *label_keys = nullptr;
  ASSERT_EQ(limit_api->FetchRuleLabelKeys(service_key, label_keys), kReturnTimeout);
  ASSERT_TRUE(label_keys == nullptr);
  delete limit_api;
}

TEST_F(LimitApiTest, GetQuota) {
  CreateConfig();
  LimitApi *limit_api = LimitApi::CreateFromConfig(config_);
  ASSERT_TRUE(limit_api != nullptr);
  QuotaRequest request;
  QuotaResponse *response = nullptr;
  ASSERT_EQ(limit_api->GetQuota(request, response), kReturnInvalidArgument);
  request.SetServiceNamespace("test");
  request.SetServiceName("test.limit.service");
  ASSERT_EQ(limit_api->InitQuotaWindow(request), kReturnTimeout);
  ASSERT_EQ(limit_api->GetQuota(request, response), kReturnTimeout);

  QuotaResultCode quota_result = kQuotaResultWait;
  ASSERT_EQ(limit_api->GetQuota(request, quota_result), kReturnTimeout);
  ASSERT_EQ(quota_result, kQuotaResultWait);

  uint64_t wait_time = 99;
  ASSERT_EQ(limit_api->GetQuota(request, quota_result, wait_time), kReturnTimeout);
  ASSERT_EQ(quota_result, kQuotaResultWait);
  ASSERT_EQ(wait_time, 99);
  delete limit_api;
}

}  // namespace polaris
