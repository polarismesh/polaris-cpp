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

#include "api/limit_api.h"

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "context/context_impl.h"
#include "logger.h"
#include "monitor/api_stat.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "polaris/limit.h"
#include "quota/quota_manager.h"
#include "quota/quota_model.h"
#include "utils/fork.h"

namespace polaris {

LimitApi::LimitApi(LimitApiImpl* impl) { impl_ = impl; }

LimitApi::~LimitApi() {
  if (impl_ != nullptr) {
    delete impl_;
    impl_ = nullptr;
  }
}

LimitApi* LimitApi::Create(Context* context) {
  std::string err_msg;
  return Create(context, err_msg);
}

LimitApi* LimitApi::Create(Context* context, std::string& err_msg) {
  if (context == nullptr) {
    err_msg = "create limit api failed because context is null";
    POLARIS_LOG(LOG_ERROR, "%s", err_msg.c_str());
    return nullptr;
  }
  if (context->GetContextMode() != kLimitContext && context->GetContextMode() != kShareContext) {
    err_msg = "create limit api failed because context isn't init with limit mode";
    POLARIS_LOG(LOG_ERROR, "%s", err_msg.c_str());
    return nullptr;
  }
  return new LimitApi(new LimitApiImpl(context));
}

LimitApi* LimitApi::CreateFromConfig(Config* config) {
  std::string err_msg;
  return CreateFromConfig(config, err_msg);
}

LimitApi* LimitApi::CreateFromConfig(Config* config, std::string& err_msg) {
  if (config == nullptr) {
    err_msg = "create limit api failed because parameter config is null";
    POLARIS_LOG(LOG_ERROR, "%s", err_msg.c_str());
    return nullptr;
  }
  Context* context = Context::Create(config, kLimitContext);
  if (context == nullptr) {
    err_msg = "create limit api failed because context create failed";
    POLARIS_LOG(LOG_ERROR, "%s", err_msg.c_str());
    return nullptr;
  }
  return LimitApi::Create(context, err_msg);
}

static LimitApi* CreateWithConfig(Config* config, std::string& err_msg) {
  if (config == nullptr) {
    POLARIS_LOG(LOG_ERROR, "init config with error: %s", err_msg.c_str());
    return nullptr;
  }
  LimitApi* limit_api = LimitApi::CreateFromConfig(config, err_msg);
  delete config;
  return limit_api;
}

LimitApi* LimitApi::CreateFromFile(const std::string& file) {
  std::string err_msg;
  return CreateFromFile(file, err_msg);
}

LimitApi* LimitApi::CreateFromFile(const std::string& file, std::string& err_msg) {
  return CreateWithConfig(Config::CreateFromFile(file, err_msg), err_msg);
}

LimitApi* LimitApi::CreateFromString(const std::string& content) {
  std::string err_msg;
  return CreateFromString(content, err_msg);
}

LimitApi* LimitApi::CreateFromString(const std::string& content, std::string& err_msg) {
  return CreateWithConfig(Config::CreateFromString(content, err_msg), err_msg);
}

LimitApi* LimitApi::CreateWithDefaultFile() {
  std::string err_msg;
  return CreateWithDefaultFile(err_msg);
}

LimitApi* LimitApi::CreateWithDefaultFile(std::string& err_msg) {
  return CreateWithConfig(Config::CreateWithDefaultFile(err_msg), err_msg);
}

LimitApiImpl::LimitApiImpl(Context* context) { context_ = context; }

LimitApiImpl::~LimitApiImpl() {
  if (context_ != nullptr && context_->GetContextMode() == kLimitContext) {
    delete context_;
  }
  context_ = nullptr;
}

ReturnCode LimitApi::GetQuota(const QuotaRequest& quota_request, QuotaResponse*& quota_response) {
  ContextImpl* context_impl = impl_->context_->GetContextImpl();
  ApiStat api_stat(context_impl, kApiStatLimitGetQuota);
  QuotaRequest::Impl& request = quota_request.GetImpl();
  ReturnCode ret_code = impl_->CheckRequest(request);
  if (ret_code != kReturnOk) {
    RECORD_THEN_RETURN(ret_code);
  }

  POLARIS_FORK_CHECK()

  QuotaManager* quota_manager = context_impl->GetQuotaManager();
  QuotaInfo quota_info;
  if ((ret_code = quota_manager->PrepareQuotaInfo(request, &quota_info)) == kReturnOk) {
    // 调用配额管理对象分配配额
    ret_code = quota_manager->GetQuotaResponse(request, quota_info, quota_response);
  }
  RECORD_THEN_RETURN(ret_code);
}

ReturnCode LimitApi::GetQuota(const QuotaRequest& quota_request, QuotaResultCode& quota_result) {
  QuotaResponse* quota_response = nullptr;
  ReturnCode ret_code;
  if ((ret_code = GetQuota(quota_request, quota_response)) == kReturnOk) {
    quota_result = quota_response->GetResultCode();
    delete quota_response;
  }
  return ret_code;
}

ReturnCode LimitApi::GetQuota(const QuotaRequest& quota_request, QuotaResultCode& quota_result,
                              QuotaResultInfo& quota_info) {
  QuotaResponse* quota_response = nullptr;
  ReturnCode ret_code;
  if ((ret_code = GetQuota(quota_request, quota_response)) == kReturnOk) {
    quota_result = quota_response->GetResultCode();
    quota_info = quota_response->GetQuotaResultInfo();
    delete quota_response;
  }
  return ret_code;
}

ReturnCode LimitApi::GetQuota(const QuotaRequest& quota_request, QuotaResultCode& quota_result, uint64_t& wait_time) {
  QuotaResponse* quota_response = nullptr;
  ReturnCode ret_code;
  if ((ret_code = GetQuota(quota_request, quota_response)) == kReturnOk) {
    quota_result = quota_response->GetResultCode();
    wait_time = quota_response->GetWaitTime();
    delete quota_response;
  }
  return ret_code;
}

ReturnCode LimitApi::FetchRule(const ServiceKey& service_key, std::string& json_rule) {
  static const uint64_t kDefaultTimeout = 1000;
  return FetchRule(service_key, kDefaultTimeout, json_rule);
}

ReturnCode LimitApi::FetchRule(const ServiceKey& service_key, uint64_t timeout, std::string& json_rule) {
  QuotaRequest quota_request;
  quota_request.SetServiceNamespace(service_key.namespace_);
  quota_request.SetServiceName(service_key.name_);
  QuotaRequest::Impl& request = quota_request.GetImpl();
  ReturnCode ret_code = impl_->CheckRequest(request);
  if (ret_code != kReturnOk) {
    return kReturnInvalidArgument;
  }
  ContextImpl* context_impl = impl_->context_->GetContextImpl();
  POLARIS_FORK_CHECK()

  quota_request.SetTimeout(timeout);
  QuotaInfo quota_info;
  QuotaManager* quota_manager = context_impl->GetQuotaManager();
  if ((ret_code = quota_manager->PrepareQuotaInfo(request, &quota_info)) == kReturnOk) {
    ServiceData* service_data = quota_info.GetServiceRateLimitRule()->GetServiceDataWithRef();
    json_rule = service_data->ToJsonString();
    service_data->DecrementRef();
  }
  return ret_code;
}

ReturnCode LimitApi::FetchRuleLabelKeys(const ServiceKey& service_key, uint64_t timeout,
                                        const std::set<std::string>*& label_keys) {
  QuotaRequest quota_request;
  quota_request.SetServiceNamespace(service_key.namespace_);
  quota_request.SetServiceName(service_key.name_);

  QuotaRequest::Impl& request = quota_request.GetImpl();
  ReturnCode ret_code = impl_->CheckRequest(request);
  if (ret_code != kReturnOk) {
    return kReturnInvalidArgument;
  }
  ContextImpl* context_impl = impl_->context_->GetContextImpl();
  POLARIS_FORK_CHECK()

  quota_request.SetTimeout(timeout);
  QuotaInfo quota_info;
  QuotaManager* quota_manager = context_impl->GetQuotaManager();
  if ((ret_code = quota_manager->PrepareQuotaInfo(request, &quota_info)) == kReturnOk) {
    ServiceRateLimitRule* limit_rule = quota_info.GetServiceRateLimitRule();
    label_keys = &limit_rule->GetLabelKeys();
  }
  return ret_code;
}

ReturnCode LimitApi::InitQuotaWindow(const QuotaRequest& quota_request) {
  QuotaRequest::Impl& request = quota_request.GetImpl();
  ReturnCode ret_code = impl_->CheckRequest(request);
  if (ret_code != kReturnOk) {
    return kReturnInvalidArgument;
  }

  ContextImpl* context_impl = impl_->context_->GetContextImpl();
  POLARIS_FORK_CHECK()

  QuotaManager* quota_manager = context_impl->GetQuotaManager();
  QuotaInfo quota_info;
  if ((ret_code = quota_manager->PrepareQuotaInfo(request, &quota_info)) == kReturnOk) {
    ret_code = quota_manager->InitWindow(request, quota_info);
  }
  return ret_code;
}

ReturnCode LimitApiImpl::CheckRequest(QuotaRequest::Impl& request) {
  if (request.service_key_.namespace_.empty()) {  // 检查请求参数
    POLARIS_LOG(LOG_ERROR, "%s request with empty service namespace", __func__);
    return kReturnInvalidArgument;
  }
  if (request.service_key_.name_.empty()) {
    POLARIS_LOG(LOG_ERROR, "%s request with empty service name", __func__);
    return kReturnInvalidArgument;
  }

  ContextImpl* context_impl = context_->GetContextImpl();
  // 设置默认参数
  if (!request.timeout_.HasValue()) {
    request.timeout_ = context_impl->GetApiDefaultTimeout();
  }
  return kReturnOk;
}

}  // namespace polaris
