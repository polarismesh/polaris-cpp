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

#include "plugin/service_router/route_result.h"

#include "model/constants.h"

namespace polaris {

RouteResult::RouteResult() : redirect_service_key_(nullptr), subset_(nullptr), new_instances_set_(false) {}

RouteResult::~RouteResult() {
  if (redirect_service_key_ != nullptr) {
    delete redirect_service_key_;
    redirect_service_key_ = nullptr;
  }
  if (subset_ != nullptr) {
    delete subset_;
    subset_ = nullptr;
  }
}

const ServiceKey& RouteResult::GetRedirectService() const { return *redirect_service_key_; }

void RouteResult::SetRedirectService(const ServiceKey& service_key) {
  if (redirect_service_key_ == nullptr) {
    redirect_service_key_ = new ServiceKey();
  }
  *redirect_service_key_ = service_key;
}

void RouteResult::SetSubset(const std::map<std::string, std::string>& subset) {
  if (subset_ == nullptr) {
    subset_ = new std::map<std::string, std::string>();
  }
  *subset_ = subset;
}

const std::map<std::string, std::string>& RouteResult::GetSubset() const {
  return subset_ == nullptr ? constants::EmptyStringMap() : *subset_;
}

}  // namespace polaris
