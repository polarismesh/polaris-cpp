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

#ifndef POLARIS_CPP_POLARIS_QUOTA_MODEL_RATE_LIMIT_RULE_H_
#define POLARIS_CPP_POLARIS_QUOTA_MODEL_RATE_LIMIT_RULE_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "model/match_string.h"
#include "polaris/defs.h"
#include "v1/model.pb.h"
#include "v1/ratelimit.pb.h"

namespace polaris {

// 限流配额
struct RateLimitAmount {
  uint32_t max_amount_;      // 时间周期内最大配额
  uint64_t valid_duration_;  // 配额生效的时间周期，必须大于1s
  uint32_t precision_;       // 统计上报精度

  uint32_t start_amount_;  // 可选，起始限流阈值，爬坡起始值
  uint32_t end_amount_;    // 可选，最大爬坡限流阈值
  uint32_t min_amount_;    // 可选，最小限流阈值，降低时最小值
};

// 限流上报配置
struct RateLimitReport {
  RateLimitReport() : interval_(0), amount_percent_(0), enable_batch_(false) {}
  uint32_t interval_;        // 配额固定上报周期，单位ms
  uint32_t amount_percent_;  // 配额使用达到百分比上报，值范围(0, 100]
  bool enable_batch_;

  uint32_t GetInterval() const { return interval_; }
};

// 限流动作类型
enum RateLimitActionType {
  kRateLimitActionReject,
  kRateLimitActionUnirate,
};

struct RateLimitWindowKey {
  std::string rule_id_;
  std::string regex_labels_;
  std::string regex_subset_;

  bool operator<(const RateLimitWindowKey& rhs) const;

  bool operator==(const RateLimitWindowKey& rhs) const;
};

class RateLimitRule {
 public:
  RateLimitRule();

  bool Init(const v1::Rule& rule);

  // 检查该规则是否能够匹配传入的subset和lables
  bool IsMatch(const std::map<std::string, std::string>& subset,
               const std::map<std::string, std::string>& labels) const;

  const std::string& GetId() const { return id_; }

  const ServiceKey& GetService() const { return service_key_; }

  uint32_t GetPriority() const { return priority_; }

  const std::string& GetRevision() const { return revision_; }

  v1::Rule::Type GetRateLimitType() const { return limit_type_; }

  bool IsGlobalLimit() const { return limit_type_ == v1::Rule::GLOBAL; }

  const RateLimitReport& GetRateLimitReport() const { return report_; }

  const std::vector<RateLimitAmount>& GetRateLimitAmount() const { return amounts_; }

  v1::Rule::AmountMode GetAmountMode() const { return amount_mode_; }

  RateLimitActionType GetActionType() const { return action_type_; }

  v1::Rule::Resource GetResourceType() const { return limit_resource_; }

  uint64_t GetMaxValidDuration() const { return max_valid_duration_; }

  const v1::AmountAdjuster& GetAdjuster() const { return adjuster_; }

  std::string GetActionString();

  std::string GetSubsetAsString();

  std::string GetLabelsAsString();

  const std::map<std::string, MatchString>& GetLabels() const { return labels_; }

  void GetWindowKey(const std::map<std::string, std::string>& subset, const std::map<std::string, std::string>& labels,
                    RateLimitWindowKey& window_key);

  std::string GetMetricId(const RateLimitWindowKey& window_key);

  v1::Rule::FailoverType GetFailoverType() { return failover_type_; }

  const ServiceKey& GetCluster() { return cluster_; }

  bool IsDisable() const { return disable_; }

 private:
  bool InitAmount(const v1::Rule& rule);

  bool InitReportConfig(const v1::Rule& rule);

  uint64_t FindMaxValidDuration();

  static bool InitMatch(const google::protobuf::Map<std::string, v1::MatchString>& pb_match,
                        std::map<std::string, MatchString>& match, bool& has_regex);

  static std::string MatchMapToStr(const std::map<std::string, MatchString>& match);

 private:
  std::string id_;
  ServiceKey service_key_;
  uint32_t priority_;  // 优先级，0是最高优先级
  v1::Rule::Resource limit_resource_;
  v1::Rule::Type limit_type_;
  std::map<std::string, MatchString> subset_;
  std::map<std::string, MatchString> labels_;
  std::vector<RateLimitAmount> amounts_;
  v1::Rule::AmountMode amount_mode_;
  RateLimitActionType action_type_;  // 限流动作
  bool disable_;                     // 是否启用
  RateLimitReport report_;
  std::string revision_;
  uint64_t max_valid_duration_;

  bool is_regex_combine_;
  v1::AmountAdjuster adjuster_;  // 动态调整配置

  v1::Rule::FailoverType failover_type_;  // 降级类型
  ServiceKey cluster_;                    // 限流集群
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_MODEL_RATE_LIMIT_RULE_H_
