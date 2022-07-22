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

#ifndef POLARIS_CPP_POLARIS_QUOTA_QUOTA_MODEL_H_
#define POLARIS_CPP_POLARIS_QUOTA_QUOTA_MODEL_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>

#include "polaris/defs.h"
#include "polaris/limit.h"
#include "quota/model/service_rate_limit_rule.h"
#include "utils/optional.h"

namespace polaris {

class QuotaRequest::Impl {
 public:
  Impl() : acquire_amount_(1) {}

  ServiceKey service_key_;
  std::map<std::string, std::string> subset_;
  std::map<std::string, std::string> labels_;
  int acquire_amount_;
  optional<uint64_t> timeout_;
};

class QuotaResponse::Impl {
 public:
  QuotaResultCode result_code_;  // 配额分配结果
  uint64_t wait_time_;           // 等待时间，单位ms
  QuotaResultInfo info_;         // 当前配额的信息

  static QuotaResponse* CreateResponse(QuotaResultCode result_code, uint64_t wait_time = 0);

  static QuotaResponse* CreateResponse(QuotaResultCode result_code, const QuotaResultInfo& info);
};

// 用于获取配额分配所需的服务数据：服务实例信息和服务限流规则
class QuotaInfo {
 public:
  void SetServiceRateLimitRule(ServiceRateLimitRule* service_rate_limit_rule) {
    service_rate_limit_rule_.reset(service_rate_limit_rule);
  }

  ServiceRateLimitRule* GetServiceRateLimitRule() const { return service_rate_limit_rule_.get(); }

 private:
  std::unique_ptr<ServiceRateLimitRule> service_rate_limit_rule_;
};

class LimitCallResult::Impl {
 public:
  Impl() : result_type_(kLimitCallResultOk), response_time_(0), response_code_(0) {}

  ServiceKey service_key_;
  std::map<std::string, std::string> subset_;
  std::map<std::string, std::string> labels_;
  LimitCallResultType result_type_;
  uint64_t response_time_;
  int response_code_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_QUOTA_MODEL_H_
