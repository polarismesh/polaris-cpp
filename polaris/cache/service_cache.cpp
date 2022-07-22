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

#include "cache/service_cache.h"

#include "context/context_impl.h"
#include "context/service_context.h"
#include "model/model_impl.h"

namespace polaris {

RouterSubsetCache::RouterSubsetCache() : instances_data_(nullptr), current_data_(nullptr) {}

RouterSubsetCache::~RouterSubsetCache() {
  if (instances_data_ != nullptr) {
    instances_data_->DecrementRef();
    instances_data_ = nullptr;
  }
  if (current_data_ != nullptr) {
    current_data_->DecrementRef();
    current_data_ = nullptr;
  }
}

RuleRouterCacheValue::RuleRouterCacheValue()
    : instances_data_(nullptr),
      route_rule_(nullptr),
      subset_sum_weight_(0),
      match_outbounds_(false),
      is_redirect_(false) {}

RuleRouterCacheValue::~RuleRouterCacheValue() {
  if (instances_data_ != nullptr) {
    instances_data_->DecrementRef();
    instances_data_ = nullptr;
  }
  if (route_rule_ != nullptr) {
    route_rule_->DecrementRef();
    route_rule_ = nullptr;
  }
  for (auto& subset : subsets_) {
    subset.second->DecrementRef();
  }
}

void ServiceCacheUpdateTask::Run() {
  context_impl_->RcuEnter();
  ServiceContext* service_context = context_impl_->GetServiceContext(service_key_);
  if (service_context != nullptr) {
    service_context->UpdateCircuitBreaker(service_key_, circuit_breaker_version_);
  }
  context_impl_->RcuExit();
}

}  // namespace polaris
