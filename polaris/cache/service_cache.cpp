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

#include <stddef.h>
#include <utility>

#include "model/model_impl.h"
namespace polaris {

RouterSubsetCache::RouterSubsetCache() : instances_data_(NULL), current_data_(NULL) {}

RouterSubsetCache::~RouterSubsetCache() {
  if (instances_data_ != NULL) {
    instances_data_->DecrementRef();
    instances_data_ = NULL;
  }
  if (current_data_ != NULL) {
    current_data_->DecrementRef();
    current_data_ = NULL;
  }
}

RuleRouterCacheValue::RuleRouterCacheValue()
    : instances_data_(NULL), route_rule_(NULL), sum_weight_(0), match_outbounds_(false) {}

RuleRouterCacheValue::~RuleRouterCacheValue() {
  if (instances_data_ != NULL) {
    instances_data_->DecrementRef();
    instances_data_ = NULL;
  }
  if (route_rule_ != NULL) {
    route_rule_->DecrementRef();
    route_rule_ = NULL;
  }
  for (std::map<uint32_t, InstancesSet*>::iterator it = data_.begin(); it != data_.end(); ++it) {
    it->second->DecrementRef();
  }
  data_.clear();
}

}  // namespace polaris
