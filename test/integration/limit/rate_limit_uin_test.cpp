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

#include <atomic>

#include "polaris/limit.h"

#include "integration/common/environment.h"
#include "integration/common/integration_base.h"
#include "utils/time_clock.h"

#include "v1/ratelimit.pb.h"

namespace polaris {

class RateLimitUinTest : public ::testing::Test {
 protected:
  RateLimitUinTest() {}

  virtual void SetUp() {
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("cpp.integration.rate.limit.uin" + std::to_string(Time::GetSystemTimeMs()));
    IntegrationBase::CreateService(service_, service_token_);

    rule_.mutable_namespace_()->set_value(service_.namespace_().value());
    rule_.mutable_service()->set_value(service_.name().value());
    rule_.mutable_service_token()->set_value(service_token_);

    config_string_ =
        "global:\n"
        "  serverConnector:\n"
        "    addresses: [" +
        Environment::GetDiscoverServer() +
        "]\nconsumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        Environment::GetPersistDir() +
        "\nrateLimiter:\n"
        "  rateLimitCluster:\n"
        "    namespace: Polaris\n"
        "    service: polaris.metric.test";
  }

  virtual void TearDown() {
    for (std::size_t i = 0; i < rule_ids_.size(); ++i) {
      IntegrationBase::DeleteRateLimitRule(rule_ids_[i], service_token_);
    }
    IntegrationBase::DeleteService(service_.name().value(), service_.namespace_().value(), service_token_);
  }

  // 创建限流规则
  void CreateRateLimitRule();

 protected:
  v1::Service service_;
  std::string service_token_;
  std::string config_string_;
  v1::Rule rule_;
  std::vector<std::string> rule_ids_;
};

void RateLimitUinTest::CreateRateLimitRule() {
  v1::Amount* amount = rule_.add_amounts();
  amount->mutable_validduration()->set_seconds(1);
  amount->mutable_maxamount()->set_value(100);

  v1::MatchString match_string;
  match_string.set_type(v1::MatchString::REGEX);
  // 0-99 使用单次上报
  match_string.mutable_value()->set_value("^\\d{1,2}$");
  (*rule_.mutable_labels())["uin"] = match_string;
  std::string rule_id;
  IntegrationBase::CreateRateLimitRule(rule_, rule_id);
  rule_ids_.push_back(rule_id);

  // 大于等于100 使用批量上报
  match_string.mutable_value()->set_value("^\\d{3,}$");
  (*rule_.mutable_labels())["uin"] = match_string;
  rule_.mutable_report()->mutable_enablebatch()->set_value(true);
  IntegrationBase::CreateRateLimitRule(rule_, rule_id);
  rule_ids_.push_back(rule_id);
}

struct RunArgs {
  std::string service_namespace;
  std::string service_name;
  std::string config_;
  bool stop_;
  std::atomic<int> ok_count_;
};

void* RunGetQuota(void* args) {
  RunArgs* run_args = static_cast<RunArgs*>(args);
  // 创建Limit API
  polaris::LimitApi* limit_api = polaris::LimitApi::CreateFromString(run_args->config_);
  EXPECT_TRUE(limit_api != nullptr);
  polaris::QuotaRequest quota_request;                             // 限流请求
  quota_request.SetServiceNamespace(run_args->service_namespace);  // 设置限流规则对应服务的命名空间
  quota_request.SetServiceName(run_args->service_name);            // 设置限流规则对应的服务名
  std::map<std::string, std::string> labels;
  polaris::ReturnCode ret;
  polaris::QuotaResultCode result;
  int limit_count = 0;
  while (!run_args->stop_) {
    for (int i = 0; i < 1000; ++i) {
      labels["uin"] = std::to_string(i);
      quota_request.SetLabels(labels);
      if ((ret = limit_api->GetQuota(quota_request, result)) != polaris::kReturnOk) {
        std::cout << "get quota for service with error:" << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
        sleep(1);
        continue;
      }
      if (result == kQuotaResultLimited) {
        limit_count++;
      }
    }
    usleep(25 * 1000);  // 每秒最多40次
  }
  std::cout << "limit count:" << limit_count << std::endl;
  EXPECT_TRUE(limit_count > 0);
  delete limit_api;
  return nullptr;
}

TEST_F(RateLimitUinTest, TestGetQuota) {
  CreateRateLimitRule();  // 创建限流规则
  sleep(3);               // 等待Discover服务器获取到服务信息

  int thread_size = 4;

  RunArgs run_args;
  run_args.service_namespace = service_.namespace_().value();
  run_args.service_name = service_.name().value();
  run_args.config_ = config_string_;
  run_args.stop_ = false;
  std::vector<pthread_t> thread_list;
  for (int i = 0; i < thread_size; i++) {
    pthread_t tid;
    pthread_create(&tid, nullptr, RunGetQuota, &run_args);
    thread_list.push_back(tid);
  }

  sleep(120);
  run_args.stop_ = true;

  for (int i = 0; i < thread_size; i++) {
    pthread_join(thread_list[i], nullptr);
  }
}

void* RunSmallQuotaLimit(void* args) {
  RunArgs* run_args = static_cast<RunArgs*>(args);
  polaris::LimitApi* limit_api = polaris::LimitApi::CreateFromString(run_args->config_);
  EXPECT_TRUE(limit_api != nullptr);
  polaris::QuotaRequest quota_request;                             // 限流请求
  quota_request.SetServiceNamespace(run_args->service_namespace);  // 设置限流规则对应服务的命名空间
  quota_request.SetServiceName(run_args->service_name);            // 设置限流规则对应的服务名
  std::map<std::string, std::string> labels;
  labels["method"] = "check";
  quota_request.SetLabels(labels);
  polaris::ReturnCode ret;
  polaris::QuotaResultCode result;
  while (!run_args->stop_) {
    if ((ret = limit_api->GetQuota(quota_request, result)) != polaris::kReturnOk) {
      std::cout << "get quota for service with error:" << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
      sleep(1);
      continue;
    }
    if (result == kQuotaResultOk) {
      run_args->ok_count_++;
    }
    sleep(1);
  }
  delete limit_api;
  return nullptr;
}

TEST_F(RateLimitUinTest, TestSmallQuotaLimit) {
  v1::Amount* amount = rule_.add_amounts();
  amount->mutable_validduration()->set_seconds(2);
  amount->mutable_maxamount()->set_value(1);

  v1::MatchString match_string;
  match_string.set_type(v1::MatchString::EXACT);
  match_string.mutable_value()->set_value("check");
  (*rule_.mutable_labels())["method"] = match_string;
  std::string rule_id;
  IntegrationBase::CreateRateLimitRule(rule_, rule_id);
  rule_ids_.push_back(rule_id);

  int thread_size = 4;
  RunArgs run_args;
  run_args.service_namespace = service_.namespace_().value();
  run_args.service_name = service_.name().value();
  run_args.config_ = config_string_;
  run_args.stop_ = false;
  run_args.ok_count_ = 0;
  std::vector<pthread_t> thread_list;
  for (int i = 0; i < thread_size; i++) {
    usleep(250 * 1000);  // 线程分散启动，让请求分散
    pthread_t tid;
    pthread_create(&tid, nullptr, RunSmallQuotaLimit, &run_args);
    thread_list.push_back(tid);
  }

  for (int i = 0; i < 10; ++i) {
    EXPECT_LE(run_args.ok_count_, i + 4);  // 允许每个线程多获取一次
    sleep(2);
  }
  run_args.stop_ = true;

  for (int i = 0; i < thread_size; i++) {
    pthread_join(thread_list[i], nullptr);
  }
}

}  // namespace polaris
