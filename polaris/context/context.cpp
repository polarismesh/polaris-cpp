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

#include "polaris/context.h"

#include <memory>

#include "context/context_impl.h"
#include "logger.h"

namespace polaris {

Context::Context(ContextImpl* impl) : impl_(impl) {}

Context::~Context() {
  if (impl_ != nullptr) {
    delete impl_;
    impl_ = nullptr;
  }
}

// 从配置中创建SDK运行上下文
Context* Context::Create(Config* config, ContextMode mode) {
  if (config == nullptr) {
    POLARIS_LOG(LOG_WARN, "create context failed because parameter config is null");
    return nullptr;
  }
  if (mode <= kNotInitContext) {
    POLARIS_LOG(LOG_WARN, "create context failed because parameter mode is NotInitContext");
    return nullptr;
  }

  ContextImpl* context_impl = new ContextImpl();
  std::unique_ptr<Context> context(new Context(context_impl));
  if (context_impl->Init(config, context.get(), mode) != kReturnOk) {
    return nullptr;
  }
  // Polaris discover先请求一下
  if (context_impl->InitSystemService(context_impl->GetDiscoverService()) != kReturnOk) {
    return nullptr;
  }
  // 如果有设置Metric Cluster则提前获取
  const PolarisCluster& metric_cluster = context_impl->GetMetricService();
  if (!metric_cluster.service_.name_.empty() && context_impl->InitSystemService(metric_cluster) != kReturnOk) {
    return nullptr;
  }
  return context.release();
}

ContextMode Context::GetContextMode() { return impl_->context_mode_; }

LocalRegistry* Context::GetLocalRegistry() { return impl_->local_registry_; }

ContextImpl* Context::GetContextImpl() { return impl_; }

}  // namespace polaris
