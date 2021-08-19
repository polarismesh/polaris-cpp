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

#include "context_internal.h"

namespace polaris {

ProviderApi::Impl::Impl(Context* context) { context_ = context; }

ProviderApi::Impl::~Impl() {
  if (context_ != NULL && context_->GetContextMode() == kPrivateContext) {
    delete context_;
  }
  context_ = NULL;
}

ProviderCallbackWrapper::ProviderCallbackWrapper(ProviderCallback* callback, ApiStat* stat)
    : callback_(callback), stat_(stat) {}

ProviderCallbackWrapper::~ProviderCallbackWrapper() {
  if (callback_ != NULL) {
    delete callback_;
    callback_ = NULL;
  }
  if (stat_ != NULL) {
    delete stat_;
    stat_ = NULL;
  }
}

void ProviderCallbackWrapper::Response(ReturnCode code, const std::string& message) {
  callback_->Response(code, message);
  stat_->Record(code);
}

}  // namespace polaris
