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

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include <algorithm>
#include <string>

#include "context/context_impl.h"
#include "logger.h"
#include "monitor/api_stat.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "polaris/defs.h"
#include "polaris/plugin.h"
#include "polaris/provider.h"
#include "provider/request.h"
#include "utils/fork.h"
#include "utils/time_clock.h"

namespace polaris {

ProviderApi::ProviderApi(ProviderApi::Impl* impl) { impl_ = impl; }

ProviderApi::~ProviderApi() {
  if (impl_ != nullptr) {
    delete impl_;
    impl_ = nullptr;
  }
}

ProviderApi* ProviderApi::Create(Context* context) {
  if (context == nullptr) {
    POLARIS_LOG(LOG_ERROR, "create provider api failed because context is null");
    return nullptr;
  }
  if (context->GetContextMode() != kPrivateContext && context->GetContextMode() != kShareContext &&
      context->GetContextMode() != kLimitContext) {
    POLARIS_LOG(LOG_ERROR, "create provider api failed because context is init with error mode");
    return nullptr;
  }
  ProviderApi::Impl* api_impl = new ProviderApi::Impl(context);
  return new ProviderApi(api_impl);
}

ProviderApi* ProviderApi::CreateFromConfig(Config* config) {
  if (config == nullptr) {
    POLARIS_LOG(LOG_WARN, "create provider api failed because parameter config is null");
    return nullptr;
  }
  Context* context = Context::Create(config, kPrivateContext);
  if (context == nullptr) {
    return nullptr;
  }
  return ProviderApi::Create(context);
}

static ProviderApi* CreateWithConfig(Config* config, const std::string& err_msg) {
  if (config == nullptr) {
    POLARIS_LOG(LOG_ERROR, "init config with error: %s", err_msg.c_str());
    return nullptr;
  }
  ProviderApi* provider = ProviderApi::CreateFromConfig(config);
  delete config;
  return provider;
}

ProviderApi* ProviderApi::CreateFromFile(const std::string& file) {
  std::string err_msg;
  return CreateWithConfig(Config::CreateFromFile(file, err_msg), err_msg);
}

ProviderApi* ProviderApi::CreateFromString(const std::string& content) {
  std::string err_msg;
  return CreateWithConfig(Config::CreateFromString(content, err_msg), err_msg);
}

ProviderApi* ProviderApi::CreateWithDefaultFile() {
  std::string err_msg;
  return CreateWithConfig(Config::CreateWithDefaultFile(err_msg), err_msg);
}

ReturnCode ProviderApi::Register(const InstanceRegisterRequest& req, std::string& instance_id) {
  ContextImpl* context_impl = impl_->context_->GetContextImpl();
  ApiStat api_stat(context_impl, kApiStatProvderRegsiter);
  ReturnCode ret_code = kReturnInvalidArgument;
  // 检查请求是否合法
  if (!req.GetImpl().CheckRequest(__func__)) {
    api_stat.Record(ret_code);
    return ret_code;
  }
  POLARIS_FORK_CHECK()
  ServerConnector* server_connector = context_impl->GetServerConnector();

  // 同步发送网络请求
  uint64_t timeout_ms = req.GetImpl().HasTimeout() ? req.GetImpl().GetTimeout() : context_impl->GetApiDefaultTimeout();
  int retry_times = context_impl->GetApiMaxRetryTimes();
  uint64_t end_time = Time::GetCoarseSteadyTimeMs() + timeout_ms;
  while (retry_times-- > 0 && timeout_ms > 0) {
    ret_code = server_connector->RegisterInstance(req, timeout_ms, instance_id);
    uint64_t current_time = Time::GetCoarseSteadyTimeMs();
    if ((ret_code != kReturnNetworkFailed && ret_code != kReturnServerError) || current_time >= end_time) {
      break;
    }
    timeout_ms = end_time - current_time;
    uint64_t backoff = std::min(timeout_ms, context_impl->GetApiRetryInterval());
    usleep(backoff * 1000);
    timeout_ms -= backoff;
  }
  api_stat.Record(ret_code);
  if (ret_code == kReturnOk) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::map<std::string, ProviderRequestBase*>::iterator it = impl_->registeredInstances_.find(instance_id);
    if (it != impl_->registeredInstances_.end()) {
      delete it->second;
      impl_->registeredInstances_.erase(it);
    }
    ProviderRequestBase* impl = new ProviderRequestBase();
    *impl = req.GetImpl();
    impl_->registeredInstances_[instance_id] = impl;
  }
  return ret_code;
}

ReturnCode ProviderApi::Deregister(const InstanceDeregisterRequest& req) {
  ContextImpl* context_impl = impl_->context_->GetContextImpl();
  ApiStat api_stat(context_impl, kApiStatProviderDeregisger);
  ReturnCode ret_code = kReturnInvalidArgument;
  if (!req.GetImpl().CheckRequest(__func__)) {  // 检查请求是否合法
    api_stat.Record(ret_code);
    return ret_code;
  }

  POLARIS_FORK_CHECK()
  ServerConnector* server_connector = context_impl->GetServerConnector();
  // 同步发送网络请求

  uint64_t timeout_ms = req.GetImpl().HasTimeout() ? req.GetImpl().GetTimeout() : context_impl->GetApiDefaultTimeout();
  int retry_times = context_impl->GetApiMaxRetryTimes();
  uint64_t end_time = Time::GetCoarseSteadyTimeMs() + timeout_ms;
  while (retry_times-- > 0 && timeout_ms > 0) {
    ret_code = server_connector->DeregisterInstance(req, timeout_ms);
    uint64_t current_time = Time::GetCoarseSteadyTimeMs();
    if ((ret_code != kReturnNetworkFailed && ret_code != kReturnServerError) || current_time >= end_time) {
      break;
    }
    timeout_ms = end_time - current_time;
    uint64_t backoff = std::min(timeout_ms, context_impl->GetApiRetryInterval());
    usleep(backoff * 1000);
    timeout_ms -= backoff;
  }
  api_stat.Record(ret_code);
  if (ret_code == kReturnOk) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::map<std::string, ProviderRequestBase*>::iterator it;
    it = impl_->registeredInstances_.find(req.GetImpl().GetInstanceID());
    if (it != impl_->registeredInstances_.end()) {
      delete it->second;
      impl_->registeredInstances_.erase(it);
    }
  }
  return ret_code;
}

ReturnCode ProviderApi::Heartbeat(const InstanceHeartbeatRequest& req) {
  ContextImpl* context_impl = impl_->context_->GetContextImpl();
  ApiStat api_stat(context_impl, kApiStatProviderHeartbeat);

  ReturnCode ret_code = kReturnInvalidArgument;
  if (!req.GetImpl().CheckRequest(__func__)) {  // 检查请求是否合法
    api_stat.Record(ret_code);
    return ret_code;
  }

  POLARIS_FORK_CHECK()
  ServerConnector* server_connector = context_impl->GetServerConnector();

  uint64_t timeout_ms = req.GetImpl().HasTimeout() ? req.GetImpl().GetTimeout() : context_impl->GetApiDefaultTimeout();
  int retry_times = context_impl->GetApiMaxRetryTimes();
  uint64_t end_time = Time::GetCoarseSteadyTimeMs() + timeout_ms;
  while (retry_times-- > 0 && timeout_ms > 0) {
    ret_code = server_connector->InstanceHeartbeat(req, timeout_ms);
    uint64_t current_time = Time::GetCoarseSteadyTimeMs();
    if ((ret_code != kReturnNetworkFailed && ret_code != kReturnServerError) || current_time >= end_time) {
      break;
    }
    timeout_ms = end_time - current_time;
    uint64_t backoff = std::min(timeout_ms, context_impl->GetApiRetryInterval());
    usleep(backoff * 1000);
    timeout_ms -= backoff;
  }
  api_stat.Record(ret_code);
  return ret_code;
}

ReturnCode ProviderApi::AsyncHeartbeat(const InstanceHeartbeatRequest& req, ProviderCallback* callback) {
  ContextImpl* context_impl = impl_->context_->GetContextImpl();
  POLARIS_FORK_CHECK()

  ApiStat* api_stat = new ApiStat(context_impl, kApiStatProviderAsyncHeartbeat);

  ReturnCode ret_code = kReturnInvalidArgument;
  InstanceHeartbeatRequest::Impl& impl = req.GetImpl();
  if (!impl.CheckRequest(__func__)) {  // 检查请求是否合法
    api_stat->Record(ret_code);
    delete api_stat;
    return ret_code;
  }

  ServerConnector* server_connector = context_impl->GetServerConnector();
  uint64_t timeout_ms = impl.HasTimeout() ? impl.GetTimeout() : context_impl->GetApiDefaultTimeout();
  ProviderCallbackWrapper* wrapper = new ProviderCallbackWrapper(callback, api_stat);
  ret_code = server_connector->AsyncInstanceHeartbeat(req, timeout_ms, wrapper);
  if (ret_code != kReturnOk) {
    api_stat->Record(ret_code);
    delete wrapper;
  }
  return ret_code;
}

}  // namespace polaris
