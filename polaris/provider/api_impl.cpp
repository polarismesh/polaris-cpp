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

#include "provider/api_impl.h"

#include "context/context_impl.h"

namespace polaris {

ProviderApi::Impl::Impl(Context* context) { context_ = context; }

ProviderApi::Impl::~Impl() {
  if (context_ != nullptr && context_->GetContextMode() == kPrivateContext) {
    delete context_;
  }
  context_ = nullptr;
  for (auto& item : registeredInstances_) {
    delete item.second;
  }
  registeredInstances_.clear();
}

ProviderCallbackWrapper::ProviderCallbackWrapper(ProviderCallback* callback, ApiStat* stat)
    : callback_(callback), stat_(stat) {}

ProviderCallbackWrapper::~ProviderCallbackWrapper() {
  if (callback_ != nullptr) {
    delete callback_;
    callback_ = nullptr;
  }
  if (stat_ != nullptr) {
    delete stat_;
    stat_ = nullptr;
  }
}

void ProviderCallbackWrapper::Response(ReturnCode code, const std::string& message) {
  callback_->Response(code, message);
  stat_->Record(code);
}

}  // namespace polaris
