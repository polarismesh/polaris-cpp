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
#include <string>

#include "polaris/defs.h"
#include "polaris/limit.h"
#include "quota/model/service_rate_limit_rule.h"
#include "utils/optional.h"
#include "utils/scoped_ptr.h"

namespace polaris {

class QuotaRequestImpl {
public:
  QuotaRequestImpl() : acquire_amount_(1) {}

  ServiceKey service_key_;
  std::map<std::string, std::string> subset_;
  std::map<std::string, std::string> labels_;
  int acquire_amount_;
  optional<uint64_t> timeout_;
};

class QuotaRequestAccessor {
public:
  explicit QuotaRequestAccessor(const QuotaRequest& request) : request_(request) {}

  const ServiceKey& GetService() const { return request_.impl_->service_key_; }

  const std::map<std::string, std::string>& GetSubset() const { return request_.impl_->subset_; }

  const std::map<std::string, std::string>& GetLabels() const { return request_.impl_->labels_; }

  int GetAcquireAmount() const { return request_.impl_->acquire_amount_; }

  bool HasTimeout() const { return request_.impl_->timeout_.HasValue(); }
  uint64_t GetTimeout() const { return request_.impl_->timeout_.Value(); }
  void SetTimeout(uint64_t timeout) { request_.impl_->timeout_ = timeout; }

private:
  const QuotaRequest& request_;
};

class QuotaResponseImpl {
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
    service_rate_limit_rule_.Reset(service_rate_limit_rule);
  }

  ServiceRateLimitRule* GetSericeRateLimitRule() const { return service_rate_limit_rule_.Get(); }

private:
  ScopedPtr<ServiceRateLimitRule> service_rate_limit_rule_;
};

class LimitCallResultImpl {
public:
  ServiceKey service_key_;
  std::map<std::string, std::string> subset_;
  std::map<std::string, std::string> labels_;
  LimitCallResultType result_type_;
  uint64_t response_time_;
  int response_code_;
};

class LimitCallResultAccessor {
public:
  explicit LimitCallResultAccessor(const LimitCallResult& call_result)
      : call_result_(call_result) {}

  const ServiceKey& GetService() const { return call_result_.impl_->service_key_; }

  const std::map<std::string, std::string>& GetSubset() const {
    return call_result_.impl_->subset_;
  }

  const std::map<std::string, std::string>& GetLabels() const {
    return call_result_.impl_->labels_;
  }

  LimitCallResultType GetCallResultType() const { return call_result_.impl_->result_type_; }

  uint64_t GetResponseTime() const { return call_result_.impl_->response_time_; }

  int GetResponseCode() const { return call_result_.impl_->response_code_; }

private:
  const LimitCallResult& call_result_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_QUOTA_MODEL_H_
