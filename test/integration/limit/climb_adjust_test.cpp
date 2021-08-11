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

#include "polaris/limit.h"

#include "integration/common/environment.h"
#include "integration/common/integration_base.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

#include "v1/ratelimit.pb.h"

namespace polaris {

// TODO 更精准的校验
#define CHECK_LIMIT(expect, seconds) \
  ((expect - 3) * seconds <= limit_) && (limit_ <= (expect + 5) * seconds)

#define CHECK_LEAST(expect, seconds) ((expect - 3) * seconds <= limit_)

#define CHECK_MOST(expect, seconds) (limit_ <= (expect + 5) * seconds)

static const int kJudgeInterval  = 2;  // 判断周期
static const int kTuneUpPeriod   = 2;  // 触发上调周期数
static const int kTuneDownPeriod = 1;  // 触发下调周期数

class ClimbAdjustTest : public IntegrationBase {
protected:
  ClimbAdjustTest() : limit_api_(NULL), limit_(0) {}

  virtual void SetUp() {
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("cpp.integration.limit.climb.adjust" +
                                       StringUtils::TypeToStr(Time::GetCurrentTimeMs()));
    IntegrationBase::SetUp();
    CreateRateLimitRule();  // 创建限流规则
    std::string content =
        "global:\n"
        "  serverConnector:\n"
        "    addresses: [" +
        Environment::GetDiscoverServer() +
        "]\n  system:\n"
        "    metricCluster:\n"
        "      namespace: Polaris\n"
        "      service: polaris.metric\n"
        "consumer:\n"
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
    std::string err_msg;
    limit_api_ = LimitApi::CreateFromString(content, err_msg);
    ASSERT_TRUE(limit_api_ != NULL) << err_msg;
    sleep(3);                                   // 等待Discover服务器获取到服务信息
    GetQuota(1, 0, 0, kJudgeInterval, limit_);  // 为了初始化
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
  void UpdateRateLimitRule(uint32_t min_amount, uint32_t start_amount, uint32_t max_amount);

  // 获取配额
  void GetQuota(int total, int error, int slow, int seconds, int& limit);

protected:
  LimitApi* limit_api_;
  std::string rule_id_;
  int limit_;
};

void ClimbAdjustTest::CreateRateLimitRuleData(v1::Rule& rule) {
  rule.mutable_namespace_()->set_value(service_.namespace_().value());
  rule.mutable_service()->set_value(service_.name().value());
  rule.mutable_service_token()->set_value(service_token_);

  v1::MatchString match_string;
  match_string.set_type(v1::MatchString::REGEX);
  match_string.mutable_value()->set_value("v*");
  (*rule.mutable_labels())["key"]  = match_string;
  (*rule.mutable_subset())["key1"] = match_string;

  v1::Amount* amount = rule.add_amounts();
  // 1s 最小30，软限60，硬限100
  amount->mutable_validduration()->set_seconds(1);
  amount->mutable_minamount()->set_value(30);
  amount->mutable_startamount()->set_value(60);
  amount->mutable_maxamount()->set_value(100);

  v1::ClimbConfig* climb_config = rule.mutable_adjuster()->mutable_climb();
  climb_config->mutable_enable()->set_value(true);
  v1::ClimbConfig::MetricConfig* metric_config = climb_config->mutable_metric();
  // 窗口：长度为5s，精度为10， 上报间隔1s，每个滑窗500ms
  metric_config->mutable_window()->set_seconds(5);
  metric_config->mutable_precision()->set_value(10);
  metric_config->mutable_reportinterval()->set_seconds(1);

  v1::ClimbConfig::TriggerPolicy* policy                = climb_config->mutable_policy();
  v1::ClimbConfig::TriggerPolicy::ErrorRate* error_rate = policy->mutable_errorrate();
  // 错误率：至少10个请求才计算错误率，错误率超过40%触发下调
  error_rate->mutable_requestvolumethreshold()->set_value(10);
  v1::ClimbConfig::TriggerPolicy::SlowRate* slow_rate = policy->mutable_slowrate();
  // 慢调用：超过1s就算慢调用，慢调用超过20%触发下调
  slow_rate->mutable_maxrt()->set_seconds(1);

  v1::ClimbConfig::ClimbThrottling* throttling = climb_config->mutable_throttling();
  // 冷水位下：下调75%，上调65%；冷水位上：下调95%，上调80%，
  // 触发上调限流比例2%，判断周期2s， 连续2次触发才上调，连续1次触发就下调
  throttling->mutable_judgeduration()->set_seconds(kJudgeInterval);  // 2s 判断一次
  throttling->mutable_tunedownperiod()->set_value(kTuneDownPeriod);  // 触发1次就下调
  throttling->mutable_limitthresholdtotuneup()->set_value(2);  // 冷水位以上2%限流上调
}

void ClimbAdjustTest::CreateRateLimitRule() {
  v1::Rule rule;
  CreateRateLimitRuleData(rule);
  IntegrationBase::CreateRateLimitRule(rule, rule_id_);
}

void ClimbAdjustTest::UpdateRateLimitRule(uint32_t min_amount, uint32_t start_amount,
                                          uint32_t max_amount) {
  v1::Rule rule;
  CreateRateLimitRuleData(rule);
  ASSERT_TRUE(!rule_id_.empty());
  rule.mutable_id()->set_value(rule_id_);
  v1::Amount* amount = rule.mutable_amounts(0);
  amount->mutable_minamount()->set_value(min_amount);
  amount->mutable_startamount()->set_value(start_amount);
  amount->mutable_maxamount()->set_value(max_amount);
  IntegrationBase::UpdateRateLimitRule(rule);
}

void ClimbAdjustTest::GetQuota(int total, int error, int slow, int seconds, int& limit) {
  QuotaRequest request;
  request.SetServiceNamespace(service_.namespace_().value());
  request.SetServiceName(service_.name().value());
  std::map<std::string, std::string> labels;
  labels.insert(std::make_pair("key", "value"));
  request.SetLabels(labels);
  std::map<std::string, std::string> subset;
  subset.insert(std::make_pair("key1", "value"));
  request.SetSubset(subset);

  LimitCallResult call_result;
  call_result.SetServiceNamespace(service_.namespace_().value());
  call_result.SetServiceName(service_.name().value());
  call_result.SetLabels(labels);
  call_result.SetSubset(subset);

  int interval = 1000 * 1000 / total;
  limit        = 0;
  for (int i = 0; i < seconds; ++i) {
    int error_count = error;
    int slow_count  = slow;
    for (int j = 0; j < total; ++j) {
      QuotaResponse* response = NULL;
      ASSERT_EQ(limit_api_->GetQuota(request, response), kReturnOk);
      if (response->GetResultCode() == kQuotaResultLimited) {
        call_result.SetResponseResult(kLimitCallResultLimited);
        limit++;
      } else {
        if (error_count-- > 0) {
          call_result.SetResponseResult(kLimitCallResultFailed);
          call_result.SetResponseCode(-1);
        } else if (slow_count-- > 0) {
          call_result.SetResponseResult(kLimitCallResultOk);
          call_result.SetResponseTime(2000);
        } else {
          call_result.SetResponseResult(kLimitCallResultOk);
          call_result.SetResponseTime(999);
        }
      }
      delete response;
      ASSERT_EQ(limit_api_->UpdateCallResult(call_result), kReturnOk);
      usleep(interval);
    }
  }
}

// 冷水位以上，超过限流数超过一定阈值，向上调整配额
TEST_F(ClimbAdjustTest, TuneUpAboveCold) {
  int seconds = kJudgeInterval * kTuneUpPeriod;
  GetQuota(80, 0, 0, seconds, limit_);  // 软限60，2个周期都超过， 触发向上调整
}

/* TODO 不再支持调整
  // 配额调整至60/80% = 75/s
  GetQuota(80, 0, 0, seconds, limit_);  // 可能已经调至75，或部分调至75
  ASSERT_TRUE(CHECK_MOST(20, seconds)) << limit_ << "/" << seconds;

  GetQuota(80, 0, 0, seconds, limit_);  // 应该已经超过75/s
  ASSERT_TRUE(CHECK_MOST(5, seconds)) << limit_ << "/" << seconds;

  // 配额调整至75/80% = 94/s
  GetQuota(80, 0, 0, seconds, limit_);
  ASSERT_TRUE(CHECK_LIMIT(0, seconds)) << limit_ << "/" << seconds;  // 阈值上调了，没有限流
}

// 冷水位以上，周期内被限流数不超过阈值，错误率和慢调用率没有超过阈值，配额不调整
TEST_F(ClimbAdjustTest, NotTuneAboveColdUnderThreshold) {
  int seconds = kJudgeInterval * kTuneUpPeriod;
  GetQuota(70, 0, 0, seconds, limit_);  // 软限60，2个周期都超过， 触发向上调整
  GetQuota(70, 0, 0, seconds, limit_);  // 已经调整至75/s
  ASSERT_TRUE(CHECK_MOST(10, seconds)) << limit_ << "/" << seconds;

  GetQuota(70, 10, 0, seconds, limit_);                              // 错误率小于40%
  ASSERT_TRUE(CHECK_LIMIT(0, seconds)) << limit_ << "/" << seconds;  // 错误率未超过阈值，配额不变

  GetQuota(70, 0, 5, seconds, limit_);                               // 慢调用率小于20%
  ASSERT_TRUE(CHECK_LIMIT(0, seconds)) << limit_ << "/" << seconds;  // 慢调用率未超过阈值，配额不变

  GetQuota(70, 0, 0, seconds, limit_);
  ASSERT_TRUE(CHECK_LIMIT(0, seconds)) << limit_ << "/" << seconds;  // 配额不变
}

// 冷水位以上，周期内错误率或者慢调用率超过一定阈值，配额按冷水位以上标准下调
TEST_F(ClimbAdjustTest, TuneDownAboveCold) {
  int seconds = kJudgeInterval * kTuneUpPeriod;
  GetQuota(80, 0, 0, seconds, limit_);  // 软限60，2个周期都超过， 触发向上调整

  // 配额调整至60/80% = 75/s
  GetQuota(80, 0, 0, seconds, limit_);  // 可能已经调至75，或部分调至75
  ASSERT_TRUE(CHECK_MOST(20, seconds)) << limit_ << "/" << seconds;

  GetQuota(80, 0, 0, seconds, limit_);  // 应该已经超过75/s
  ASSERT_TRUE(CHECK_MOST(5, seconds)) << limit_ << "/" << seconds;

  // 配额调整至75/80% = 94/s
  GetQuota(80, 0, 0, seconds, limit_);
  ASSERT_TRUE(CHECK_LIMIT(0, seconds)) << limit_ << "/" << seconds;  // 阈值上调了，没有限流

  GetQuota(90, 0, 0, seconds, limit_);  // 配额上调至100/s
  ASSERT_TRUE(CHECK_LIMIT(0, seconds)) << limit_ << "/" << seconds;

  // 冷水位以上下调过程，当前配额100/s
  seconds = kJudgeInterval * kTuneDownPeriod;
  GetQuota(100, 90, 0, seconds, limit_);
  GetQuota(100, 90, 0, seconds, limit_);  // 错误率大于40%
  ASSERT_TRUE(CHECK_LEAST(0, seconds)) << limit_ << "/" << seconds;

  // 配额下调至100*95%=95/s
  GetQuota(100, 0, 90, seconds, limit_);  // 慢调用率大于20%
  ASSERT_TRUE(CHECK_LEAST(0, seconds)) << limit_ << "/" << seconds;

  // 配额下调至95*95%=90/s
  GetQuota(100, 0, 30, seconds, limit_);  // 慢调用率大于20%
  ASSERT_TRUE(CHECK_LEAST(5, seconds)) << limit_ << "/" << seconds;

  // 配额下调至90*95%=85/s
  GetQuota(100, 0, 30, seconds, limit_);
  ASSERT_TRUE(CHECK_LEAST(10, seconds)) << limit_ << "/" << seconds;

  // 配额下调至85*95%=80/s
  GetQuota(100, 0, 30, seconds, limit_);
  ASSERT_TRUE(CHECK_LEAST(15, seconds)) << limit_ << "/" << seconds;
}

// 冷水位以下，周期内错误率或者慢调用率超过一定阈值，QPS阈值按冷水位以下标准下调
// 调到最小值以后不再往下调整
TEST_F(ClimbAdjustTest, TuneDownBelowCold) {
  int seconds = kJudgeInterval * kTuneDownPeriod;
  GetQuota(50, 40, 0, seconds, limit_);  // 错误率大于40%，触发向下调整
  ASSERT_TRUE(CHECK_LIMIT(0, seconds)) << limit_ << "/" << seconds;

  // 配额调整 60*75%=45/s
  GetQuota(50, 40, 0, seconds, limit_);
  ASSERT_TRUE(CHECK_LEAST(0, seconds)) << limit_ << "/" << seconds;

  // 配额调整 45*75%=33/s
  GetQuota(50, 0, 40, seconds, limit_);  // 慢调用率大于20%，触发向下调整
  ASSERT_TRUE(CHECK_LEAST(5, seconds)) << limit_ << "/" << seconds;

  // 配额调整 33*75%=24，小于最小值30
  GetQuota(50, 0, 40, seconds, limit_);  //慢调用率大于20%，触发向下调整
  ASSERT_TRUE(CHECK_LEAST(17, seconds)) << limit_ << "/" << seconds;

  // 当前配额 30
  GetQuota(50, 0, 20, seconds, limit_);  // 阈值小于最大值不再调整
  ASSERT_TRUE(CHECK_LEAST(20, seconds)) << limit_ << "/" << seconds;
}

// 冷水位以下，周期内错误率或者慢调用率不超过阈值，QPS阈值按冷水位往下标准上调
TEST_F(ClimbAdjustTest, TuneUpBelowCold) {
  int seconds = kJudgeInterval * kTuneDownPeriod;
  GetQuota(50, 40, 0, seconds, limit_);  // 错误率大于40%，触发向下调整
  ASSERT_TRUE(CHECK_LIMIT(0, seconds)) << limit_ << "/" << seconds;

  // 配额调整 60*75%=45/s
  GetQuota(50, 40, 0, seconds, limit_);
  ASSERT_TRUE(CHECK_LEAST(0, seconds)) << limit_ << "/" << seconds;

  // 配额调整 45*75%=33/s
  GetQuota(50, 0, 40, seconds, limit_);  // 慢调用率大于20%，触发向下调整
  ASSERT_TRUE(CHECK_LEAST(5, seconds)) << limit_ << "/" << seconds;

  // 配额调整 33*75%=24，小于最小值30
  GetQuota(60, 0, 20, seconds, limit_);  //慢调用率大于20%，触发向下调整
  ASSERT_TRUE(CHECK_LEAST(17, seconds)) << limit_ << "/" << seconds;

  // 触发上调
  GetQuota(60, 0, 0, seconds, limit_);
  GetQuota(60, 0, 0, seconds, limit_);
  GetQuota(60, 0, 0, seconds, limit_);
  ASSERT_TRUE(CHECK_MOST(30, seconds)) << limit_ << "/" << seconds;

  // 配额调整为 30/65%=47
  GetQuota(60, 0, 0, seconds, limit_);
  ASSERT_TRUE(CHECK_MOST(30, seconds)) << limit_ << "/" << seconds;

  // 配额调整为 47/65%=73，超过软限，重置为软限60
  GetQuota(60, 0, 0, seconds, limit_);                               // 慢调用率小于20%
  ASSERT_TRUE(CHECK_MOST(13, seconds)) << limit_ << "/" << seconds;  // 配额上调

  GetQuota(59, 1, 1, seconds, limit_);
  ASSERT_TRUE(CHECK_LIMIT(0, seconds)) << limit_ << "/" << seconds;
}

// 限流规则发生变更，在下一个统计周期，按照新的规则进行限流判断
TEST_F(ClimbAdjustTest, UpdateRateLimitAmount) {
  int seconds = kJudgeInterval * kTuneUpPeriod;
  for (int i = 0; i < 7; i++) {  // 达到硬限100
    GetQuota(120, 0, 0, seconds, limit_);
  }
  ASSERT_TRUE(CHECK_LIMIT(20, seconds)) << limit_ << "/" << seconds;

  UpdateRateLimitRule(100, 150, 200);  // 更新规则
  sleep(3);
  GetQuota(100, 0, 0, seconds, limit_);  // 等待规则更新

  GetQuota(130, 0, 0, seconds, limit_);  // 新规则不限流
  ASSERT_TRUE(CHECK_LIMIT(0, seconds)) << limit_ << "/" << seconds;

  UpdateRateLimitRule(150, 200, 250);  // 更新规则
  sleep(3);
  GetQuota(100, 0, 0, seconds, limit_);  // 等待规则更新

  GetQuota(180, 0, 0, seconds, limit_);  // 新规则不限流
  ASSERT_TRUE(CHECK_LIMIT(0, seconds)) << limit_ << "/" << seconds;
}*/

}  // namespace polaris
