//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
//  language governing permissions and limitations under the License.
//

#include "quota/model/rate_limit_rule.h"

#include <memory>
#include <ostream>
#include <utility>

#include "logger.h"
#include "model/model_impl.h"
#include "polaris/model.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

namespace polaris {

bool RateLimitWindowKey::operator<(const RateLimitWindowKey& rhs) const {
  if (this->rule_id_ < rhs.rule_id_) {
    return true;
  } else if (this->rule_id_ > rhs.rule_id_) {
    return false;
  } else if (this->regex_labels_ < rhs.regex_labels_) {
    return true;
  } else if (this->regex_labels_ > rhs.regex_labels_) {
    return false;
  } else {
    return this->regex_subset_ < rhs.regex_subset_;
  }
}

bool RateLimitWindowKey::operator==(const RateLimitWindowKey& rhs) const {
  return this->rule_id_ == rhs.rule_id_ && this->regex_subset_ == rhs.regex_subset_ &&
         this->regex_labels_ == rhs.regex_labels_;
}

RateLimitRule::RateLimitRule()
    : priority_(0), limit_resource_(v1::Rule::QPS), limit_type_(v1::Rule::GLOBAL),
      amount_mode_(v1::Rule::GLOBAL_TOTAL), action_type_(kRateLimitActionReject), disable_(true),
      max_valid_duration_(0), is_regex_combine_(true), failover_type_(v1::Rule::FAILOVER_LOCAL) {}

bool RateLimitRule::Init(const v1::Rule& rule) {
  disable_ = rule.has_disable() ? rule.disable().value() : false;
  if (disable_) {
    return false;  // 禁用的规则不用保存
  }
  id_       = rule.id().value();
  priority_ = rule.has_priority() ? rule.priority().value() : 0;  // 未设置则为0表示最高优先级
  service_key_.name_      = rule.service().value();
  service_key_.namespace_ = rule.namespace_().value();
  // 显示指定了并发限流时使用并发限流，其他情况或默认为QPS限流
  limit_resource_ = rule.resource();
  limit_type_     = rule.type();

  bool has_regex = false;
  if (!InitMatch(rule.labels(), labels_, has_regex) ||
      !InitMatch(rule.subset(), subset_, has_regex)) {
    return false;
  }
  if (has_regex) {
    is_regex_combine_ = rule.has_regex_combine() ? rule.regex_combine().value() : false;
  }
  if (!InitAmount(rule)) {  // 未配置任何限流周期，则解析失败
    return false;
  }
  amount_mode_ = rule.amount_mode();
  // 默认reject
  if (!rule.has_action() || StringUtils::IgnoreCaseCmp(rule.action().value(), "reject")) {
    action_type_ = kRateLimitActionReject;
  } else if (StringUtils::IgnoreCaseCmp(rule.action().value(), "unirate")) {
    action_type_ = kRateLimitActionUnirate;
  } else {  // 其他类型暂未支持
    return false;
  }
  if (!InitReportConfig(rule)) {
    return false;
  }
  revision_           = rule.revision().value();
  max_valid_duration_ = FindMaxValidDuration();
  failover_type_      = rule.failover();
  if (rule.has_cluster()) {
    cluster_.namespace_ = rule.cluster().namespace_().value();
    cluster_.name_      = rule.cluster().service().value();
  }

  adjuster_.CopyFrom(rule.adjuster());
  return true;
}

bool RateLimitRule::InitAmount(const v1::Rule& rule) {
  for (int i = 0; i < rule.amounts_size(); ++i) {
    const v1::Amount& rule_amount = rule.amounts(i);
    RateLimitAmount amount;
    amount.valid_duration_ = Time::DurationToUint64(rule_amount.validduration());
    if (amount.valid_duration_ < 1000) {  // 最小的配额周期为1s
      return false;
    }
    amount.max_amount_ = rule_amount.maxamount().value();
    amount.precision_  = rule_amount.has_precision() ? rule_amount.precision().value() : 1;

    if (rule_amount.has_startamount()) {  // 有软限流配置
      amount.start_amount_ = rule_amount.startamount().value();
      amount.end_amount_   = amount.max_amount_;    // 硬限等于配额
      amount.max_amount_   = amount.start_amount_;  // 默认配额等于软限
      amount.min_amount_   = rule_amount.has_minamount() ? rule_amount.minamount().value() : 1;
    } else {  // 未配置软限时不根据健康度调整配额
      amount.start_amount_ = amount.max_amount_;
      amount.end_amount_   = amount.max_amount_;
      amount.min_amount_   = rule_amount.has_minamount() ? rule_amount.minamount().value() : 1;
    }
    amounts_.push_back(amount);
  }
  return !amounts_.empty();
}

bool RateLimitRule::InitMatch(const google::protobuf::Map<std::string, v1::MatchString>& pb_match,
                              std::map<std::string, MatchString>& match, bool& has_regex) {
  for (google::protobuf::Map<std::string, v1::MatchString>::const_iterator it = pb_match.begin();
       it != pb_match.end(); ++it) {
    MatchString& match_string = match[it->first];
    if (!match_string.Init(it->second)) {
      return false;
    }
    if (match_string.IsRegex()) {
      has_regex = true;
    }
  }
  return true;
}

bool RateLimitRule::InitReportConfig(const v1::Rule& rule) {
  static const uint64_t kDefaultLimitReportInterval = 100;       // 默认上报间隔
  static const uint64_t kMinRateLimitReportInterval = 20;        // 最小限流上报周期
  static const uint64_t kMaxRateLimitReportInterval = 5 * 1000;  // 最大限流上报周期
  static const uint64_t kRateLimitReportAmountPresent = 80;  // 默认满足百分之80的请求后立刻限流上报
  static const uint64_t kMaxRateLimitReportAmountPresent = 100;  // 最大实时上报百分比
  static const uint64_t kMinRateLimitReportAmountPresent = 0;    // 最小实时上报百分比

  POLARIS_ASSERT(!amounts_.empty());  // 必须在Amount已经解析后调用
  report_.amount_percent_ = kRateLimitReportAmountPresent;
  if (rule.has_report() && rule.report().has_amountpercent()) {
    report_.amount_percent_ = rule.report().amountpercent().value();
    if (report_.amount_percent_ <= kMinRateLimitReportAmountPresent ||
        report_.amount_percent_ > kMaxRateLimitReportAmountPresent) {
      return false;  // 上报百分比例不在(0, 100]范围内
    }
  }
  if (rule.has_report() && rule.report().has_interval()) {
    report_.interval_ = Time::DurationToUint64(rule.report().interval());
    if (report_.interval_ < kMinRateLimitReportInterval) {
      report_.interval_ = kMinRateLimitReportInterval;
    }
    if (report_.interval_ > kMaxRateLimitReportInterval) {
      report_.interval_ = kMaxRateLimitReportInterval;
    }
  } else {
    report_.interval_ = kDefaultLimitReportInterval;
  }
  return true;
}

uint64_t RateLimitRule::FindMaxValidDuration() {
  POLARIS_ASSERT(!amounts_.empty());  // 必须在Amount已经解析后调用
  uint64_t max_duration = amounts_[0].valid_duration_;
  for (std::size_t i = 1; i < amounts_.size(); ++i) {
    if (max_duration < amounts_[i].valid_duration_) {
      max_duration = amounts_[i].valid_duration_;
    }
  }
  return max_duration;
}

bool RateLimitRule::IsMatch(const std::map<std::string, std::string>& subset,
                            const std::map<std::string, std::string>& labels) const {
  if (disable_) {  // 规则被禁用
    return false;
  }
  return MatchString::MapMatch(labels_, labels) && MatchString::MapMatch(subset_, subset);
}

std::string RateLimitRule::GetActionString() {
  return action_type_ == kRateLimitActionReject ? "reject" : "unirate";
}

std::string RateLimitRule::MatchMapToStr(const std::map<std::string, MatchString>& match) {
  std::ostringstream output;
  const char* separator = "";
  std::map<std::string, MatchString>::const_iterator it;
  for (it = match.begin(); it != match.end(); ++it) {
    output << separator << it->first << ":" << it->second.GetString();
    separator = ";";
  }
  return output.str();
}

std::string RateLimitRule::GetSubsetAsString() {
  return subset_.empty() ? "*" : MatchMapToStr(subset_);
}

std::string RateLimitRule::GetLabelsAsString() {
  return labels_.empty() ? "*" : MatchMapToStr(labels_);
}

std::string RateLimitRule::GetMetricId(const RateLimitWindowKey& window_key) {
  std::string output = id_;
  output += "#";

  std::map<std::string, MatchString>::const_iterator it;
  const char* separator = "";
  std::size_t pos       = 0;
  for (it = subset_.begin(); it != subset_.end(); ++it) {
    output += separator;
    output += it->first;
    output += ":";
    if (it->second.IsExactText()) {
      output += it->second.GetString();
    } else {
      std::size_t next_pos = window_key.regex_subset_.find('|', pos);
      if (next_pos != std::string::npos) {
        output += window_key.regex_subset_.substr(pos, next_pos - pos);
        pos = next_pos + 1;
      } else {
        output += window_key.regex_subset_.substr(pos);
      }
    }
    separator = "|";
  }

  output += "#";

  separator = "";
  pos       = 0;
  for (it = labels_.begin(); it != labels_.end(); ++it) {
    output += separator;
    output += it->first;
    output += ":";
    if (it->second.IsExactText()) {
      output += it->second.GetString();
    } else {
      std::size_t next_pos = window_key.regex_labels_.find('|', pos);
      if (next_pos != std::string::npos) {
        output += window_key.regex_labels_.substr(pos, next_pos - pos);
        pos = next_pos + 1;
      } else {
        output += window_key.regex_labels_.substr(pos);
      }
    }
    separator = "|";
  }
  return output;
}

void RateLimitRule::GetWindowKey(const std::map<std::string, std::string>& subset,
                                 const std::map<std::string, std::string>& labels,
                                 RateLimitWindowKey& window_key) {
  window_key.rule_id_   = id_;
  const char* separator = "";
  std::map<std::string, MatchString>::const_iterator it;
  std::map<std::string, std::string>::const_iterator match_it;
  for (it = subset_.begin(); it != subset_.end(); ++it) {
    if (it->second.IsRegex()) {
      match_it = subset.find(it->first);
      window_key.regex_subset_ += (separator + match_it->second);
      separator = "|";
    }
  }
  separator = "";
  for (it = labels_.begin(); it != labels_.end(); ++it) {
    if (it->second.IsRegex()) {
      match_it = labels.find(it->first);
      window_key.regex_labels_ += (separator + match_it->second);
      separator = "|";
    }
  }
}

}  // namespace polaris
