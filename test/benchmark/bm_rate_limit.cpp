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

#include "benchmark/context_fixture.h"

#include <iostream>

#include "polaris/limit.h"
#include "utils/string_utils.h"

// 本文件用于限流API的性能测试，包括以下方面的测试：
//   - LimitApi获取配额接口QPS测试
//   - 程序调用LimitAPI获取配额QPS损失
//   - 规则数量对获取配额QPS的影响

namespace polaris {

class BM_RateLimit : public ContextFixture {
public:
  void SetUp(::benchmark::State &state) {
    if (state.thread_index != 0) return;

    if (config_.find("rateLimiter") == std::string::npos) {
      config_.append("\nrateLimiter:\n  mode: local");
    }
    context_mode_ = kLimitContext;
    ContextFixture::SetUp(state);
    limit_api_ = LimitApi::Create(context_);
    if (limit_api_ == NULL) {
      std::cout << "create limit api failed" << std::endl;
      abort();
    }
    srand(time(NULL));

    service_key_.namespace_ = "Test";
    service_key_.name_      = "rate.limit.rule.match";
  }

  void TearDown(::benchmark::State &state) {
    if (state.thread_index != 0) return;

    if (limit_api_ != NULL) {
      delete limit_api_;
      limit_api_ = NULL;
    }
    ContextFixture::TearDown(state);
  }

  ReturnCode InitServiceData(const ServiceKey &service_key) {
    v1::DiscoverResponse response;
    FakeServer::CreateServiceRateLimit(response, service_key, 200000000);
    ReturnCode ret_code = ContextFixture::LoadData(response);
    if (ret_code != kReturnOk) {
      return ret_code;
    }
    FakeServer::CreateServiceInstances(response, service_key, 1);
    return ContextFixture::LoadData(response);
  }

  ReturnCode InitServiceData(const ServiceKey &service_key, int uin_count) {
    v1::DiscoverResponse response;
    response.mutable_code()->set_value(v1::ExecuteSuccess);
    response.set_type(v1::DiscoverResponse::RATE_LIMIT);
    v1::Service *service = response.mutable_service();
    service->mutable_namespace_()->set_value(service_key.namespace_);
    service->mutable_name()->set_value(service_key.name_);
    service->mutable_revision()->set_value("init_version");
    v1::RateLimit *rate_limit = response.mutable_ratelimit();
    rate_limit->mutable_revision()->set_value("version_one");
    for (int i = 0; i < uin_count; ++i) {
      v1::Rule *rule = rate_limit->add_rules();
      rule->mutable_id()->set_value(StringUtils::TypeToStr(i));
      rule->mutable_namespace_()->set_value(service_key.namespace_);
      rule->mutable_service()->set_value(service_key.name_);
      rule->set_type(v1::Rule::LOCAL);
      v1::MatchString match_string;
      match_string.set_type(v1::MatchString::EXACT);
      match_string.mutable_value()->set_value(StringUtils::TypeToStr(123456789 + i));
      (*rule->mutable_labels())["uin"] = match_string;
      v1::Amount *amount               = rule->add_amounts();
      amount->mutable_maxamount()->set_value(100000);
      amount->mutable_validduration()->set_seconds(1);
    }
    ReturnCode ret_code = ContextFixture::LoadData(response);
    if (ret_code != kReturnOk) {
      return ret_code;
    }
    FakeServer::CreateServiceInstances(response, service_key, 1);
    return ContextFixture::LoadData(response);
  }

protected:
  LimitApi *limit_api_;
  ServiceKey service_key_;
};

// 测试LimitApi获取配额接口的性能
BENCHMARK_DEFINE_F(BM_RateLimit, GetQuotaQps)(benchmark::State &state) {
  ReturnCode ret_code;
  if (state.thread_index == 0) {
    if ((ret_code = InitServiceData(service_key_)) != kReturnOk) {
      std::string err_msg = "init service failed:" + ReturnCodeToMsg(ret_code);
      state.SkipWithError(err_msg.c_str());
    }
  }
  QuotaRequest request;
  request.SetServiceNamespace(service_key_.namespace_);
  request.SetServiceName(service_key_.name_);
  std::map<std::string, std::string> labels;
  labels.insert(std::make_pair("label", "value"));
  request.SetLabels(labels);
  std::map<std::string, std::string> subset;
  subset.insert(std::make_pair("subset", "value"));
  request.SetSubset(subset);
  QuotaResultCode quota_result;
  while (state.KeepRunning()) {
    if ((ret_code = limit_api_->GetQuota(request, quota_result)) != kReturnOk) {
      std::string err_msg = "get quota failed:" + ReturnCodeToMsg(ret_code);
      state.SkipWithError(err_msg.c_str());
      break;
    }
    if (quota_result == kQuotaResultLimited) {
      state.SkipWithError("quota limited");
      break;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_RateLimit, GetQuotaQps)
    ->ThreadRange(1, 8)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->UseRealTime();

// 测试LimitApi给业务程序带来的性能损失
BENCHMARK_DEFINE_F(BM_RateLimit, GetQuotaQpsLoss)(benchmark::State &state) {
  ReturnCode ret_code;
  if (state.thread_index == 0) {
    if ((ret_code = InitServiceData(service_key_)) != kReturnOk) {
      std::string err_msg = "init service failed:" + ReturnCodeToMsg(ret_code);
      state.SkipWithError(err_msg.c_str());
    }
  }
  QuotaRequest request;
  request.SetServiceNamespace(service_key_.namespace_);
  request.SetServiceName(service_key_.name_);
  std::map<std::string, std::string> labels;
  labels.insert(std::make_pair("label", "value"));
  request.SetLabels(labels);
  std::map<std::string, std::string> subset;
  subset.insert(std::make_pair("subset", "value"));
  request.SetSubset(subset);
  QuotaResultCode quota_result;
  while (state.KeepRunning()) {
    if (state.range(1) == 1) {
      if ((ret_code = limit_api_->GetQuota(request, quota_result)) != kReturnOk) {
        std::string err_msg = "get quota failed:" + ReturnCodeToMsg(ret_code);
        state.SkipWithError(err_msg.c_str());
        break;
      }
      if (quota_result == kQuotaResultLimited) {
        state.SkipWithError("quota limited");
        break;
      }
    }
    int array_length = state.range(0);
    std::vector<int> array(array_length);
    for (int i = 0; i < array_length; ++i) {
      array.push_back(rand() % 1000000);
    }
    std::sort(array.begin(), array.end());
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_RateLimit, GetQuotaQpsLoss)
    ->ArgNames({"array_num", "limit_flag"})
    ->Args({25, 0})
    ->Args({25, 1})
    ->Args({50, 0})
    ->Args({50, 1})
    ->Args({100, 0})
    ->Args({100, 1})
    ->Args({200, 0})
    ->Args({200, 1})
    ->Args({500, 0})
    ->Args({500, 1})
    ->Args({1000, 0})
    ->Args({1000, 1})
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->UseRealTime();

// 测试不同数量的路由规则对限流API的性能影响
BENCHMARK_DEFINE_F(BM_RateLimit, LimitRuleMatch)(benchmark::State &state) {
  int uin_count = state.range(0);
  ReturnCode ret_code;
  if (state.thread_index == 0) {
    if ((ret_code = InitServiceData(service_key_, uin_count)) != kReturnOk) {
      std::string err_msg = "init service failed:" + ReturnCodeToMsg(ret_code);
      state.SkipWithError(err_msg.c_str());
    }
  }

  while (state.KeepRunning()) {
    QuotaRequest request;
    request.SetServiceNamespace(service_key_.namespace_);
    request.SetServiceName(service_key_.name_);
    std::map<std::string, std::string> labels;
    labels.insert(std::make_pair("uin", StringUtils::TypeToStr(123456789 + rand() % uin_count)));
    request.SetLabels(labels);
    QuotaResultCode quota_result;
    if ((ret_code = limit_api_->GetQuota(request, quota_result)) != kReturnOk) {
      std::string err_msg = "get quota failed:" + ReturnCodeToMsg(ret_code);
      state.SkipWithError(err_msg.c_str());
      break;
    }
    if (quota_result == kQuotaResultLimited) {
      state.SkipWithError("quota limited");
      break;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_RateLimit, LimitRuleMatch)
    ->ArgName("rule_num")
    ->Arg(10)
    ->Arg(20)
    ->Arg(50)
    ->Arg(100)
    ->Arg(200)
    ->Arg(500)
    ->Arg(1000)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->UseRealTime();

}  // namespace polaris
