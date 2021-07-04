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

#include "context_internal.h"
#include "logger.h"
#include "monitor/api_stat.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "polaris/limit.h"
#include "quota/quota_manager.h"
#include "quota/quota_model.h"

namespace polaris {

LimitApi::LimitApi(LimitApiImpl* impl) { impl_ = impl; }

LimitApi::~LimitApi() {
  if (impl_ != NULL) {
    delete impl_;
    impl_ = NULL;
  }
}

LimitApi* LimitApi::Create(Context* context) {
  std::string err_msg;
  return Create(context, err_msg);
}

LimitApi* LimitApi::Create(Context* context, std::string& err_msg) {
  if (context == NULL) {
    err_msg = "create limit api failed because context is null";
    POLARIS_LOG(LOG_ERROR, "%s", err_msg.c_str());
    return NULL;
  }
  if (context->GetContextMode() != kLimitContext) {
    err_msg = "create limit api failed because context isn't init with limit mode";
    POLARIS_LOG(LOG_ERROR, "%s", err_msg.c_str());
    return NULL;
  }
  return new LimitApi(new LimitApiImpl(context));
}

LimitApi* LimitApi::CreateFromConfig(Config* config) {
  std::string err_msg;
  return CreateFromConfig(config, err_msg);
}

LimitApi* LimitApi::CreateFromConfig(Config* config, std::string& err_msg) {
  if (config == NULL) {
    err_msg = "create limit api failed because parameter config is null";
    POLARIS_LOG(LOG_ERROR, "%s", err_msg.c_str());
    return NULL;
  }
  Context* context = Context::Create(config, kLimitContext);
  if (context == NULL) {
    err_msg = "create limit api failed because context create failed";
    POLARIS_LOG(LOG_ERROR, "%s", err_msg.c_str());
    return NULL;
  }
  return LimitApi::Create(context, err_msg);
}

static LimitApi* CreateWithConfig(Config* config, std::string& err_msg) {
  if (config == NULL) {
    POLARIS_LOG(LOG_ERROR, "init config with error: %s", err_msg.c_str());
    return NULL;
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
  if (context_ != NULL && context_->GetContextMode() == kLimitContext) {
    delete context_;
  }
  context_ = NULL;
}

ReturnCode LimitApi::GetQuota(const QuotaRequest& quota_request, QuotaResponse*& quota_response) {
  ApiStat api_stat(impl_->context_, kApiStatLimitGetQuota);
  QuotaRequestAccessor request(quota_request);
  ReturnCode ret_code = impl_->CheckRequest(request);
  if (ret_code != kReturnOk) {
    RECORD_THEN_RETURN(ret_code);
  }

  QuotaManager* quota_manager = impl_->context_->GetContextImpl()->GetQuotaManager();
  QuotaInfo quota_info;
  if ((ret_code = quota_manager->PrepareQuotaInfo(quota_request, &quota_info)) == kReturnOk) {
    // 调用配额管理对象分配配额
    ret_code = quota_manager->GetQuotaResponse(quota_request, quota_info, quota_response);
  }
  RECORD_THEN_RETURN(ret_code);
}

ReturnCode LimitApi::GetQuota(const QuotaRequest& quota_request, QuotaResultCode& quota_result) {
  QuotaResponse* quota_response = NULL;
  ReturnCode ret_code;
  if ((ret_code = GetQuota(quota_request, quota_response)) == kReturnOk) {
    quota_result = quota_response->GetResultCode();
    delete quota_response;
  }
  return ret_code;
}

ReturnCode LimitApi::GetQuota(const QuotaRequest& quota_request, QuotaResultCode& quota_result,
                              QuotaResultInfo& quota_info) {
  QuotaResponse* quota_response = NULL;
  ReturnCode ret_code;
  if ((ret_code = GetQuota(quota_request, quota_response)) == kReturnOk) {
    quota_result = quota_response->GetResultCode();
    quota_info   = quota_response->GetQuotaResultInfo();
    delete quota_response;
  }
  return ret_code;
}

ReturnCode LimitApi::GetQuota(const QuotaRequest& quota_request, QuotaResultCode& quota_result,
                              uint64_t& wait_time) {
  QuotaResponse* quota_response = NULL;
  ReturnCode ret_code;
  if ((ret_code = GetQuota(quota_request, quota_response)) == kReturnOk) {
    quota_result = quota_response->GetResultCode();
    wait_time    = quota_response->GetWaitTime();
    delete quota_response;
  }
  return ret_code;
}

ReturnCode LimitApi::UpdateCallResult(const LimitCallResult& call_result) {
  ApiStat api_stat(impl_->context_, kApiStatLimitUpdateCallResult);
  LimitCallResultAccessor request(call_result);
  if (request.GetService().namespace_.empty()) {  // 检查请求参数
    POLARIS_LOG(LOG_ERROR, "%s request with empty service namespace", __func__);
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }
  if (request.GetService().name_.empty()) {
    POLARIS_LOG(LOG_ERROR, "%s request with empty service name", __func__);
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }
  // 更新使用结果
  ReturnCode ret_code =
      impl_->context_->GetContextImpl()->GetQuotaManager()->UpdateCallResult(call_result);
  RECORD_THEN_RETURN(ret_code);
}

ReturnCode LimitApi::FetchRule(const ServiceKey& service_key, std::string& json_rule) {
  static const uint64_t kDefaultTimeout = 1000;
  return FetchRule(service_key, kDefaultTimeout, json_rule);
}

ReturnCode LimitApi::FetchRule(const ServiceKey& service_key, uint64_t timeout,
                               std::string& json_rule) {
  QuotaRequest quota_request;
  quota_request.SetServiceNamespace(service_key.namespace_);
  quota_request.SetServiceName(service_key.name_);
  QuotaRequestAccessor request(quota_request);
  ReturnCode ret_code = impl_->CheckRequest(request);
  if (ret_code != kReturnOk) {
    return kReturnInvalidArgument;
  }
  quota_request.SetTimeout(timeout);
  QuotaInfo quota_info;
  QuotaManager* quota_manager = impl_->context_->GetContextImpl()->GetQuotaManager();
  if ((ret_code = quota_manager->PrepareQuotaInfo(quota_request, &quota_info)) == kReturnOk) {
    ServiceData* service_data = quota_info.GetSericeRateLimitRule()->GetServiceDataWithRef();
    json_rule                 = service_data->ToJsonString();
    service_data->DecrementRef();
  }
  return ret_code;
}

ReturnCode LimitApi::FetchRuleLabelKeys(const ServiceKey& service_key, uint64_t timeout,
                                        const std::set<std::string>*& label_keys) {
  QuotaRequest quota_request;
  quota_request.SetServiceNamespace(service_key.namespace_);
  quota_request.SetServiceName(service_key.name_);

  QuotaRequestAccessor request(quota_request);
  ReturnCode ret_code = impl_->CheckRequest(request);
  if (ret_code != kReturnOk) {
    return kReturnInvalidArgument;
  }
  quota_request.SetTimeout(timeout);
  QuotaInfo quota_info;
  QuotaManager* quota_manager = impl_->context_->GetContextImpl()->GetQuotaManager();
  if ((ret_code = quota_manager->PrepareQuotaInfo(quota_request, &quota_info)) == kReturnOk) {
    ServiceRateLimitRule* limit_rule = quota_info.GetSericeRateLimitRule();
    label_keys                       = &limit_rule->GetLabelKeys();
  }
  return ret_code;
}

ReturnCode LimitApi::InitQuotaWindow(const QuotaRequest& quota_request) {
  QuotaRequestAccessor request(quota_request);
  ReturnCode ret_code = impl_->CheckRequest(request);
  if (ret_code != kReturnOk) {
    return kReturnInvalidArgument;
  }

  QuotaManager* quota_manager = impl_->context_->GetContextImpl()->GetQuotaManager();
  QuotaInfo quota_info;
  if ((ret_code = quota_manager->PrepareQuotaInfo(quota_request, &quota_info)) == kReturnOk) {
    ret_code = quota_manager->InitWindow(quota_request, quota_info);
  }
  return ret_code;
}

ReturnCode LimitApiImpl::CheckRequest(QuotaRequestAccessor& request) {
  if (request.GetService().namespace_.empty()) {  // 检查请求参数
    POLARIS_LOG(LOG_ERROR, "%s request with empty service namespace", __func__);
    return kReturnInvalidArgument;
  }
  if (request.GetService().name_.empty()) {
    POLARIS_LOG(LOG_ERROR, "%s request with empty service name", __func__);
    return kReturnInvalidArgument;
  }

  ContextImpl* context_impl = context_->GetContextImpl();
  // 设置默认参数
  if (!request.HasTimeout()) {
    request.SetTimeout(context_impl->GetApiDefaultTimeout());
  }
  return kReturnOk;
}

}  // namespace polaris
