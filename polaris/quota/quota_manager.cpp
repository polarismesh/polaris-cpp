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

#include "quota/quota_manager.h"

#include <features.h>
#include <stddef.h>
#include <stdint.h>
#include <v1/request.pb.h>

#include "context_internal.h"
#include "logger.h"
#include "metric/metric_connector.h"
#include "model/constants.h"
#include "monitor/api_stat.h"
#include "polaris/config.h"
#include "polaris/limit.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "quota/model/rate_limit_rule.h"
#include "quota/rate_limit_connector.h"
#include "quota/rate_limit_window.h"
#include "reactor/task.h"
#include "utils/time_clock.h"

namespace polaris {

uint32_t RateLimitWindowKeyHash(const RateLimitWindowKey& key) {
  uint32_t s = MurmurString(key.rule_id_);
  // see https://stackoverflow.com/q/4948780
  if (!key.regex_labels_.empty()) {
    s ^= MurmurString(key.regex_labels_) + 0x9e3779b9 + (s << 6) + (s >> 2);
  }
  if (!key.regex_subset_.empty()) {
    s ^= MurmurString(key.regex_subset_) + 0x9e3779b9 + (s << 6) + (s >> 2);
  }
  return s;
}

QuotaManager::QuotaManager()
    : context_(NULL), rate_limit_mode_(kRateLimitDisable), task_thread_id_(0),
      rate_limit_connector_(NULL), metric_connector_(NULL), rate_limit_window_lru_(NULL) {}

QuotaManager::~QuotaManager() {
  reactor_.Stop();
  if (task_thread_id_ != 0) {
    pthread_join(task_thread_id_, NULL);
    task_thread_id_ = 0;
  }
  if (rate_limit_connector_ != NULL) {
    delete rate_limit_connector_;
    rate_limit_connector_ = NULL;
  }
  if (metric_connector_ != NULL) {
    delete metric_connector_;
    metric_connector_ = NULL;
  }
  if (rate_limit_window_lru_ != NULL) {
    delete rate_limit_window_lru_;
    rate_limit_window_lru_ = NULL;
  }
  context_ = NULL;
}

ReturnCode QuotaManager::Init(Context* context, Config* config) {
  static const char kRateLimitEnableKey[]   = "enable";
  static const bool kRateLimitEnableDefault = true;

  context_                 = context;
  ContextMode context_mode = context->GetContextMode();
  if (context_mode == kPrivateContext) {  // provider或consumer私有时不创建quota manger线程
    return kReturnOk;
  }
  bool is_enable = config->GetBoolOrDefault(kRateLimitEnableKey, kRateLimitEnableDefault);
  if (is_enable == false) {
    return kReturnOk;
  }
  static const char kRateLimitModeKey[]    = "mode";
  static const char kRateLimitGlobalMode[] = "global";
  static const char kRateLimitLocalMode[]  = "local";
  std::string mode = config->GetStringOrDefault(kRateLimitModeKey, kRateLimitGlobalMode);
  if (mode == kRateLimitGlobalMode) {
    rate_limit_mode_ = kRateLimitGlobal;
    ReturnCode ret_code;
    if ((ret_code = InitLimitService(config, context_mode)) != kReturnOk) {
      return ret_code;
    }
  } else if (mode == kRateLimitLocalMode) {
    rate_limit_mode_ = kRateLimitLocal;
  } else {
    POLARIS_LOG(LOG_ERROR, "rate limit mode must be [%s, %s]", kRateLimitGlobalMode,
                kRateLimitLocalMode);
    return kReturnInvalidConfig;
  }

  static const char kRateLimitLruSizeKey[]   = "lruSize";
  static const bool kRateLimitLruSizeDefault = 0;
  int lru_size = config->GetIntOrDefault(kRateLimitLruSizeKey, kRateLimitLruSizeDefault);
  if (lru_size > 0) {
    rate_limit_window_lru_ =
        new LruHashMap<RateLimitWindowKey, RateLimitWindow>(lru_size, RateLimitWindowKeyHash);
  }

  metric_connector_ = new MetricConnector(reactor_, context_);
  if (task_thread_id_ == 0) {
    if (pthread_create(&task_thread_id_, NULL, RunTask, this) != 0) {
      POLARIS_LOG(LOG_ERROR, "create quota manager task thread error");
      return kReturnInvalidState;
    }
#if defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 12)
    pthread_setname_np(task_thread_id_, "quota_mgr");
#endif
    POLARIS_LOG(LOG_INFO, "create quota manager task thread success");
  }
  return kReturnOk;
}

ReturnCode QuotaManager::InitLimitService(Config* config, ContextMode context_mode) {
  static const char kRateLimitClusterKey[]   = "rateLimitCluster";
  static const char kRateLimitNamespaceKey[] = "namespace";
  static const char kRateLimitServiceKey[]   = "service";

  Config* service_config = config->GetSubConfig(kRateLimitClusterKey);
  ServiceKey rate_limit_service;
  rate_limit_service.namespace_ = service_config->GetStringOrDefault(kRateLimitNamespaceKey, "");
  rate_limit_service.name_      = service_config->GetStringOrDefault(kRateLimitServiceKey, "");
  delete service_config;
  static const char kMessageTimeoutKey[]       = "messageTimeout";
  static const uint64_t kMessageTimeoutDefault = 1000;
  uint64_t message_timeout = config->GetMsOrDefault(kMessageTimeoutKey, kMessageTimeoutDefault);

  if (context_mode == kLimitContext) {  // 只有在LimitContext模式下才检查服务是否配置
    if (rate_limit_service.namespace_.empty()) {
      POLARIS_LOG(LOG_ERROR, "rate limit cluster namespace is empty");
      return kReturnInvalidConfig;
    }
    if (rate_limit_service.name_.empty()) {
      POLARIS_LOG(LOG_ERROR, "rate limit cluster service is empty");
      return kReturnInvalidConfig;
    }
    if (rate_limit_service.name_ == "polaris.metric" &&
        rate_limit_service.namespace_ == constants::kPolarisNamespace) {
      POLARIS_LOG(LOG_ERROR,
                  "distribute rate limit service[Polaris/polaris.metric] is disabled, connect to "
                  "polaris helper please");
      return kReturnInvalidConfig;
    }
    ServiceKey metric_service = context_->GetContextImpl()->GetMetricService().service_;
    if (rate_limit_service == metric_service) {
      POLARIS_LOG(
          LOG_ERROR,
          "cannot use system metric service[%s/%s] as distribute rate limit service, connect "
          "to polaris helper please",
          metric_service.namespace_.c_str(), metric_service.name_.c_str());
      return kReturnInvalidConfig;
    }
  }
  rate_limit_connector_ = new RateLimitConnector(reactor_, context_, message_timeout);
  return rate_limit_connector_->InitService(rate_limit_service);
}

static const uint64_t kRateLimitWindowClearInterval = 10 * 1000;  // 清理过期窗口检查周期

void* QuotaManager::RunTask(void* quota_manager) {
  QuotaManager* quota_manager_ = static_cast<QuotaManager*>(quota_manager);
  quota_manager_->reactor_.AddTimingTask(new TimingFuncTask<QuotaManager>(
      ClearExpiredWindow, quota_manager_, kRateLimitWindowClearInterval));
  quota_manager_->reactor_.Run();
  return NULL;
}

void QuotaManager::ClearExpiredWindow(QuotaManager* quota_manager) {
  std::vector<RateLimitWindow*> all_windows;
  if (quota_manager->rate_limit_window_lru_ == NULL) {
    quota_manager->rate_limit_window_cache_.GetAllValuesWithRef(all_windows);
  } else {
    quota_manager->rate_limit_window_lru_->GetAllValuesWithRef(all_windows);
  }
  for (std::size_t i = 0; i < all_windows.size(); i++) {
    if (all_windows[i]->IsExpired()) {
      all_windows[i]->MakeDeleted();
      if (quota_manager->rate_limit_window_lru_ == NULL) {
        quota_manager->rate_limit_window_cache_.Delete(all_windows[i]->GetCacheKey());
      } else {
        quota_manager->rate_limit_window_lru_->Delete(all_windows[i]->GetCacheKey());
      }
    }
    all_windows[i]->DecrementRef();
  }
  all_windows.clear();

  quota_manager->rate_limit_window_cache_.CheckGc(
      quota_manager->context_->GetContextImpl()->RcuMinTime() - 2000);
  // 再设置下次检查任务
  quota_manager->reactor_.AddTimingTask(new TimingFuncTask<QuotaManager>(
      ClearExpiredWindow, quota_manager, kRateLimitWindowClearInterval));
}

ReturnCode QuotaManager::GetQuota(const QuotaRequest& quota_request, const QuotaInfo& quota_info,
                                  QuotaResponse*& quota_response) {
  ApiStat api_stat(context_, kApiStatLimitGetQuota);
  ReturnCode ret_code = GetQuotaResponse(quota_request, quota_info, quota_response);
  RECORD_THEN_RETURN(ret_code);
}

ReturnCode QuotaManager::GetQuotaResponse(const QuotaRequest& quota_request,
                                          const QuotaInfo& quota_info,
                                          QuotaResponse*& quota_response) {
  if (rate_limit_mode_ == kRateLimitDisable) {  // 未开启流控
    quota_response = QuotaResponseImpl::CreateResponse(kQuotaResultOk);
    return kReturnOk;
  }
  QuotaRequestAccessor request(quota_request);
  uint64_t begin_time                = Time::GetCurrentTimeMs();
  RateLimitWindow* rate_limit_window = NULL;
  ReturnCode ret_code                = GetRateLimitWindow(request, quota_info, rate_limit_window);
  if (ret_code != kReturnOk) {
    if (ret_code == kReturnResourceNotFound) {  // 没有匹配到限流规则，则不进行限流
      quota_response = QuotaResponseImpl::CreateResponse(kQuotaResultOk);
      return kReturnOk;
    }
    return ret_code;
  }
  uint64_t end_time = Time::GetCurrentTimeMs();
  if (end_time >= begin_time + request.GetTimeout()) {
    rate_limit_window->DecrementRef();
    return kReturnTimeout;
  }

  uint64_t timeout = begin_time + request.GetTimeout() - end_time;
  // 等待状态变成已初始化
  if ((ret_code = rate_limit_window->WaitRemoteInit(timeout)) == kReturnOk) {
    quota_response = rate_limit_window->AllocateQuota(request.GetAcquireAmount());
  } else {
    POLARIS_LOG(LOG_ERROR, "wait rate limit window init with error:%s",
                ReturnCodeToMsg(ret_code).c_str());
  }
  rate_limit_window->DecrementRef();
  return ret_code;
}

ReturnCode QuotaManager::PrepareQuotaInfo(const QuotaRequest& quota_request,
                                          QuotaInfo* quota_info) {
  QuotaRequestAccessor request(quota_request);
  ServiceData* rate_limit_data              = NULL;
  ServiceDataNotify* rate_limit_data_notify = NULL;
  LocalRegistry* local_registry             = context_->GetLocalRegistry();

  // 获取服务限流规则失败，获取Notify
  if (local_registry->GetServiceDataWithRef(request.GetService(), kServiceDataRateLimit,
                                            rate_limit_data) != kReturnOk) {
    local_registry->LoadServiceDataWithNotify(request.GetService(), kServiceDataRateLimit,
                                              rate_limit_data, rate_limit_data_notify);
  }
  ReturnCode ret_code = kReturnOk;
  timespec ts         = Time::CurrentTimeAddWith(request.GetTimeout());
  if (ret_code == kReturnOk && rate_limit_data_notify != NULL) {
    ret_code = rate_limit_data_notify->WaitDataWithRefUtil(ts, rate_limit_data);
  }
  if (ret_code == kReturnOk) {  // 两种数据都加载成功
    if (rate_limit_data->GetDataStatus() != kDataNotFound) {
      quota_info->SetServiceRateLimitRule(new ServiceRateLimitRule(rate_limit_data));
      return kReturnOk;
    } else {
      ret_code = kReturnServiceNotFound;
    }
  }
  if (rate_limit_data != NULL) {
    rate_limit_data->DecrementRef();
  }
  return ret_code;
}

ReturnCode QuotaManager::InitWindow(const QuotaRequest& quota_request,
                                    const QuotaInfo& quota_info) {
  QuotaRequestAccessor request(quota_request);
  RateLimitWindow* rate_limit_window = NULL;
  ReturnCode ret_code;
  if ((ret_code = GetRateLimitWindow(request, quota_info, rate_limit_window)) != kReturnOk) {
    return ret_code;
  }
  // 等待状态变成已初始化
  if ((ret_code = rate_limit_window->WaitRemoteInit(request.GetTimeout())) != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "wait rate limit window init with error:%s",
                ReturnCodeToMsg(ret_code).c_str());
  }
  rate_limit_window->DecrementRef();
  return ret_code;
}

ReturnCode QuotaManager::GetRateLimitWindow(QuotaRequestAccessor& quota_request,
                                            const QuotaInfo& quota_info,
                                            RateLimitWindow*& rate_limit_window) {
  QuotaRequestAccessor request(quota_request);
  RateLimitRule* rate_limit_rule = quota_info.GetSericeRateLimitRule()->MatchRateLimitRule(
      request.GetSubset(), request.GetLabels());
  if (rate_limit_rule == NULL) {
    return kReturnResourceNotFound;
  }
  RateLimitWindowKey window_key;
  rate_limit_rule->GetWindowKey(request.GetSubset(), request.GetLabels(), window_key);

  // 通过ID查找window
  RateLimitWindow* cached_window = rate_limit_window_lru_ == NULL
                                       ? rate_limit_window_cache_.Get(window_key)
                                       : rate_limit_window_lru_->Get(window_key);
  if (cached_window != NULL) {
    if (cached_window->CheckRateLimitRuleRevision(rate_limit_rule->GetRevision())) {
      rate_limit_window = cached_window;
      return kReturnOk;
    } else {
      cached_window->DecrementRef();
      cached_window = NULL;
    }
  }

  sync::MutexGuard mutex_guard(window_init_lock_);  // 加锁进行后续初始化操作
  // 再检查一遍，防止其他线程已经初始化
  cached_window = rate_limit_window_lru_ == NULL ? rate_limit_window_cache_.Get(window_key)
                                                 : rate_limit_window_lru_->Get(window_key);
  if (cached_window != NULL) {  // 被其他线程创建
    if (cached_window->CheckRateLimitRuleRevision(rate_limit_rule->GetRevision())) {
      rate_limit_window = cached_window;
      return kReturnOk;
    } else {
      if (rate_limit_window_lru_ == NULL) {
        rate_limit_window_cache_.Delete(window_key);
      } else {
        rate_limit_window_lru_->Delete(window_key);
      }
      cached_window->MakeDeleted();
      cached_window->DecrementRef();
      cached_window = NULL;
    }
  }
  // 创建并初始化window
  cached_window = new RateLimitWindow(reactor_, metric_connector_, window_key);
  ReturnCode ret_code;
  std::string metric_id = rate_limit_rule->GetMetricId(window_key);
  if ((ret_code = cached_window->Init(quota_info.GetSericeRateLimitRule()->GetServiceDataWithRef(),
                                      rate_limit_rule, metric_id, rate_limit_connector_)) !=
      kReturnOk) {
    cached_window->DecrementRef();
    return ret_code;
  }
  cached_window->IncrementRef();
  if (rate_limit_window_lru_ == NULL) {
    rate_limit_window_cache_.Update(window_key, cached_window);
  } else {
    rate_limit_window_lru_->Update(window_key, cached_window);
  }
  rate_limit_window = cached_window;  // 刚创建不需要检查实例数
  return ret_code;
}

ReturnCode QuotaManager::UpdateCallResult(const LimitCallResult& call_result) {
  LimitCallResultAccessor request(call_result);
  ServiceData* rate_limit_data = NULL;

  ReturnCode ret_code = context_->GetLocalRegistry()->GetServiceDataWithRef(
      request.GetService(), kServiceDataRateLimit, rate_limit_data);
  if (ret_code != kReturnOk) {
    return kReturnNotInit;
  }

  ServiceRateLimitRule service_rate_limit_rule(rate_limit_data);
  RateLimitRule* rate_limit_rule =
      service_rate_limit_rule.MatchRateLimitRule(request.GetSubset(), request.GetLabels());
  if (rate_limit_rule == NULL) {
    return kReturnNotInit;
  }

  // 通过ID查找window
  RateLimitWindowKey window_key;
  rate_limit_rule->GetWindowKey(request.GetSubset(), request.GetLabels(), window_key);
  RateLimitWindow* cached_window = rate_limit_window_lru_ == NULL
                                       ? rate_limit_window_cache_.Get(window_key)
                                       : rate_limit_window_lru_->Get(window_key);
  if (cached_window == NULL) {
    return kReturnNotInit;
  }
  cached_window->UpdateCallResult(call_result);
  cached_window->DecrementRef();
  return kReturnOk;
}

void QuotaManager::CollectRecord(
    google::protobuf::RepeatedField<v1::RateLimitRecord>& report_data) {
  if (context_ != NULL) {
    std::vector<RateLimitWindow*> all_windows;
    ContextImpl* context_impl = context_->GetContextImpl();
    context_impl->RcuEnter();
    if (rate_limit_window_lru_ == NULL) {
      rate_limit_window_cache_.GetAllValuesWithRef(all_windows);
    } else {
      rate_limit_window_lru_->GetAllValuesWithRef(all_windows);
    }
    for (std::size_t i = 0; i < all_windows.size(); i++) {
      v1::RateLimitRecord record;
      if (all_windows[i]->CollectRecord(record)) {
        report_data.Add(record);
      }
      all_windows[i]->DecrementRef();
    }
    all_windows.clear();
    context_impl->RcuExit();
  }
}

}  // namespace polaris
