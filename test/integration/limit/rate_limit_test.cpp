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

#include <gtest/gtest.h>
#include <pthread.h>

#include "polaris/limit.h"

#include "integration/common/environment.h"
#include "integration/common/integration_base.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

#include "v1/ratelimit.pb.h"

namespace polaris {

// 配额1/10的精度
#define CHECK_LIMIT(expect, seconds, quota) \
  ((expect - (quota / 10)) * seconds <= limit_) && (limit_ <= (expect + (quota / 10)) * seconds)

class RateLimitTest : public IntegrationBase {
protected:
  RateLimitTest() : limit_api_(NULL), limit_(0) {}

  virtual void SetUp() {
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("cpp.integration.rate.limit" +
                                       StringUtils::TypeToStr(Time::GetCurrentTimeMs()));
    IntegrationBase::SetUp();
    CreateRateLimitRule();  // 创建限流规则
    std::string content =
        "global:\n"
        "  serverConnector:\n"
        "    addresses: [" +
        Environment::GetDiscoverServer() +
        "]\nconsumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        Environment::GetPersistDir() +
        "\n  circuitBreaker:\n"
        "    setCircuitBreaker:\n"
        "      enable: true\n"
        "rateLimiter:\n"
        "  rateLimitCluster:\n"
        "    namespace: Polaris\n"
        "    service: polaris.metric.test";
    limit_api_ = LimitApi::CreateFromString(content);
    ASSERT_TRUE(limit_api_ != NULL);
    sleep(3);                // 等待Discover服务器获取到服务信息
    GetQuota(1, 1, limit_);  // 为了初始化
    ASSERT_EQ(limit_, 0) << limit_;
  }

  virtual void TearDown() {
    if (limit_api_ != NULL) {
      delete limit_api_;
      limit_api_ = NULL;
    }
    if (!rule_id_.empty()) {  // 删除限流规则
      IntegrationBase::DeleteRateLimitRule(rule_id_, service_token_);
    }
    IntegrationBase::TearDown();
  }

  // 创建限流规则数据
  void CreateRateLimitRuleData(v1::Rule& rule);

  // 创建限流规则
  void CreateRateLimitRule();

  // 更新限流规则
  void UpdateRateLimitRule(uint32_t max_amount);

  void UpdateRateLimitRuleRegexCombine(bool regex_combine = true);

public:
  // 获取配额
  void GetQuota(int total, int seconds, int& limit, int acquire_amount = 1,
                std::string label = "value");

protected:
  LimitApi* limit_api_;
  std::string rule_id_;
  int limit_;
};

void RateLimitTest::CreateRateLimitRuleData(v1::Rule& rule) {
  rule.mutable_namespace_()->set_value(service_.namespace_().value());
  rule.mutable_service()->set_value(service_.name().value());
  rule.mutable_service_token()->set_value(service_token_);

  v1::MatchString match_string;
  match_string.set_type(v1::MatchString::REGEX);
  match_string.mutable_value()->set_value("v*");
  (*rule.mutable_labels())["label"]  = match_string;
  (*rule.mutable_subset())["subset"] = match_string;

  v1::Amount* amount = rule.add_amounts();
  amount->mutable_validduration()->set_seconds(1);
  amount->mutable_maxamount()->set_value(100);
}

void RateLimitTest::CreateRateLimitRule() {
  v1::Rule rule;
  CreateRateLimitRuleData(rule);
  IntegrationBase::CreateRateLimitRule(rule, rule_id_);
}

void RateLimitTest::UpdateRateLimitRule(uint32_t max_amount) {
  v1::Rule rule;
  CreateRateLimitRuleData(rule);
  ASSERT_TRUE(!rule_id_.empty());
  rule.mutable_id()->set_value(rule_id_);
  v1::Amount* amount = rule.mutable_amounts(0);
  amount->mutable_maxamount()->set_value(max_amount);
  IntegrationBase::UpdateRateLimitRule(rule);
}

void RateLimitTest::UpdateRateLimitRuleRegexCombine(bool regex_combine) {
  v1::Rule rule;
  CreateRateLimitRuleData(rule);
  ASSERT_TRUE(!rule_id_.empty());
  rule.mutable_id()->set_value(rule_id_);
  rule.mutable_regex_combine()->set_value(regex_combine);
  IntegrationBase::UpdateRateLimitRule(rule);
}

void RateLimitTest::GetQuota(int total, int seconds, int& limit, int acquire_amount,
                             std::string label) {
  QuotaRequest request;
  request.SetServiceNamespace(service_.namespace_().value());
  request.SetServiceName(service_.name().value());
  std::map<std::string, std::string> labels;
  labels.insert(std::make_pair("label", label));
  request.SetLabels(labels);
  std::map<std::string, std::string> subset;
  subset.insert(std::make_pair("subset", "value"));
  request.SetSubset(subset);
  request.SetAcquireAmount(acquire_amount);

  int interval = 1000 * 1000 / total;
  limit        = 0;
  for (int i = 0; i < seconds; ++i) {
    for (int j = 0; j < total; ++j) {
      QuotaResultCode quota_result;
      if (j % 2 == 0) {
        QuotaResponse* response = NULL;
        ASSERT_EQ(limit_api_->GetQuota(request, response), kReturnOk);
        quota_result                = response->GetResultCode();
        const QuotaResultInfo& info = response->GetQuotaResultInfo();
        if (quota_result == kQuotaResultLimited) {
          ASSERT_EQ(info.left_quota_, 0);
        } else {
          ASSERT_GE(info.left_quota_, 0);
        }
        ASSERT_GT(info.all_quota_, 0);
        ASSERT_GT(info.duration_, 0);
        delete response;
      } else {
        ASSERT_EQ(limit_api_->GetQuota(request, quota_result), kReturnOk);
      }
      if (quota_result == kQuotaResultLimited) {
        limit++;
      }
      usleep(interval);
    }
  }
}

// 检查多个配额限流
TEST_F(RateLimitTest, FetchRule) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  std::string json_rule;
  ReturnCode ret_code = limit_api_->FetchRule(service_key, 1000, json_rule);
  ASSERT_EQ(ret_code, kReturnOk);
  ASSERT_TRUE(json_rule.size() > 0);
  const std::set<std::string>* label_keys;
  ret_code = limit_api_->FetchRuleLabelKeys(service_key, 0, label_keys);
  ASSERT_EQ(ret_code, kReturnOk);
  ASSERT_TRUE(label_keys != NULL);
  ASSERT_TRUE(label_keys->count("label") > 0);
}

// 检查限流
TEST_F(RateLimitTest, CheckRateLimit) {
  int seconds = 3;
  int total   = 95;
  GetQuota(total, seconds, limit_);
  ASSERT_TRUE(CHECK_LIMIT(0, seconds, total)) << limit_ << "/" << seconds;

  total = 100;
  GetQuota(total, seconds, limit_);
  ASSERT_TRUE(CHECK_LIMIT(0, seconds, total)) << limit_ << "/" << seconds;

  total = 105;
  GetQuota(total, 1, limit_);
  GetQuota(total, seconds, limit_);
  ASSERT_TRUE(CHECK_LIMIT(5, seconds, total)) << limit_ << "/" << seconds;
}

// 检查多个配额限流
TEST_F(RateLimitTest, CheckAcquireAmountLimit) {
  int seconds        = 3;
  int total          = 45;
  int acquire_amount = 2;
  GetQuota(total, seconds, limit_, acquire_amount);
  ASSERT_TRUE(CHECK_LIMIT(0, seconds, total)) << limit_ << "/" << seconds;

  total = 50;
  GetQuota(total, seconds, limit_, acquire_amount);
  ASSERT_TRUE(CHECK_LIMIT(0, seconds, total)) << limit_ << "/" << seconds;

  total = 55;
  GetQuota(total, 1, limit_, acquire_amount);
  GetQuota(total, seconds, limit_, acquire_amount);
  ASSERT_TRUE(CHECK_LIMIT(5, seconds, total)) << limit_ << "/" << seconds;
}

struct RateLimitTestArg {
  RateLimitTest* rate_limit_test;
  std::string label;
  pthread_t tid;
};

void* RegexSeparateFunc(void* arg) {
  RateLimitTestArg* func_arg = static_cast<RateLimitTestArg*>(arg);
  int seconds                = 3;
  int total                  = 45;
  int acquire_amount         = 2;
  int limit_;
  func_arg->rate_limit_test->GetQuota(total, seconds, limit_, acquire_amount, func_arg->label);
  EXPECT_TRUE(CHECK_LIMIT(0, seconds, total)) << limit_ << "/" << seconds;

  total = 50;
  func_arg->rate_limit_test->GetQuota(total, seconds, limit_, acquire_amount, func_arg->label);
  EXPECT_TRUE(CHECK_LIMIT(0, seconds, total)) << limit_ << "/" << seconds;

  total = 55;
  func_arg->rate_limit_test->GetQuota(total, 1, limit_, acquire_amount, func_arg->label);
  func_arg->rate_limit_test->GetQuota(total, seconds, limit_, acquire_amount, func_arg->label);
  EXPECT_TRUE(CHECK_LIMIT(5, seconds, total)) << limit_ << "/" << seconds;
  return NULL;
}

TEST_F(RateLimitTest, CheckRegexSeparate) {
  RateLimitTestArg thread_arg[4];
  for (int i = 0; i < 4; ++i) {
    thread_arg[i].label           = "lab" + StringUtils::TypeToStr(i);
    thread_arg[i].rate_limit_test = this;
    int rc = pthread_create(&thread_arg[i].tid, NULL, RegexSeparateFunc, &thread_arg[i]);
    ASSERT_EQ(rc, 0);
  }
  for (int i = 0; i < 4; ++i) {
    int rc = pthread_join(thread_arg[i].tid, NULL);
    ASSERT_EQ(rc, 0);
  }
}

void* RegexCombineFunc(void* arg) {
  RateLimitTestArg* func_arg = static_cast<RateLimitTestArg*>(arg);
  int seconds                = 3;
  int total                  = 45;
  int acquire_amount         = 1;
  int limit_;
  func_arg->rate_limit_test->GetQuota(total, seconds, limit_, acquire_amount, func_arg->label);
  EXPECT_TRUE(CHECK_LIMIT(0, seconds, total)) << limit_ << "/" << seconds;

  total = 50;
  func_arg->rate_limit_test->GetQuota(total, seconds, limit_, acquire_amount, func_arg->label);
  EXPECT_TRUE(CHECK_LIMIT(0, seconds, total)) << limit_ << "/" << seconds;

  total = 55;
  func_arg->rate_limit_test->GetQuota(total, 1, limit_, acquire_amount, func_arg->label);
  func_arg->rate_limit_test->GetQuota(total, seconds, limit_, acquire_amount, func_arg->label);
  EXPECT_TRUE(CHECK_LIMIT(5, seconds, total)) << limit_ << "/" << seconds;
  return NULL;
}

TEST_F(RateLimitTest, CheckRegexCombine) {
  UpdateRateLimitRuleRegexCombine();
  RateLimitTestArg thread_arg[2];
  for (int i = 0; i < 2; ++i) {
    thread_arg[i].label           = "lab" + StringUtils::TypeToStr(i);
    thread_arg[i].rate_limit_test = this;
    int rc = pthread_create(&thread_arg[i].tid, NULL, RegexCombineFunc, &thread_arg[i]);
    ASSERT_EQ(rc, 0);
  }
  for (int i = 0; i < 2; ++i) {
    int rc = pthread_join(thread_arg[i].tid, NULL);
    ASSERT_EQ(rc, 0);
  }
}

}  // namespace polaris
