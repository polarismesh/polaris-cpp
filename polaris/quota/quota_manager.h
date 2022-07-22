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

#ifndef POLARIS_CPP_POLARIS_QUOTA_QUOTA_MANAGER_H_
#define POLARIS_CPP_POLARIS_QUOTA_QUOTA_MANAGER_H_

#include <pthread.h>
#include <v1/request.pb.h>

#include <map>
#include <mutex>
#include <string>

#include "cache/lru_map.h"
#include "cache/rcu_map.h"
#include "polaris/context.h"
#include "quota/model/service_rate_limit_rule.h"
#include "quota/quota_model.h"
#include "reactor/reactor.h"

namespace polaris {

class MetricConnector;
class RateLimitConnector;
class RateLimitWindow;

enum RateLimitMode {
  kRateLimitDisable = 0,  // 不使用限流
  kRateLimitLocal = 1,    // 只是要本地限流
  kRateLimitGlobal = 2    // 使用分布式限流
};

// 配额管理，一个Context初始化一个本对象
class QuotaManager {
 public:
  QuotaManager();
  ~QuotaManager();

  // 初始化
  ReturnCode Init(Context* context, Config* config);

  // 获取配额，会统计API调用结果，目前提供tRPC调用
  ReturnCode GetQuota(const QuotaRequest::Impl& request, const QuotaInfo& quota_info, QuotaResponse*& quota_response);

  // 更新调用结果
  ReturnCode UpdateCallResult(const LimitCallResult::Impl& request);

  Reactor& GetReactor() { return reactor_; }

  MetricConnector* GetMetricConnector() { return metric_connector_; }

  // 获取服务实例和限流规则数据
  ReturnCode PrepareQuotaInfo(const QuotaRequest::Impl& request, QuotaInfo* quota_info);

  // 初始化window
  ReturnCode InitWindow(const QuotaRequest::Impl& request, const QuotaInfo& quota_info);

  void CollectRecord(google::protobuf::RepeatedField<v1::RateLimitRecord>& report_data);

  ReturnCode GetQuotaResponse(const QuotaRequest::Impl& request, const QuotaInfo& quota_info,
                              QuotaResponse*& quota_response);

 private:
  // 通过请求获取获取限流窗口
  ReturnCode GetRateLimitWindow(const QuotaRequest::Impl& request, const QuotaInfo& quota_info,
                                RateLimitWindow*& rate_limit_window);

  // 限流管理线程主循环
  static void* RunTask(void* quota_manager);

  // 检查限流窗口对应的规则是否生效
  bool CheckRuleEnable(RateLimitWindow* rate_limit_window);

  // 定期清理超过一定时间未访问的限流窗口
  static void ClearExpiredWindow(QuotaManager* quota_manager);

 private:
  Context* context_;
  Reactor reactor_;
  bool is_enable_;
  pthread_t task_thread_id_;

  RateLimitConnector* rate_limit_connector_;
  MetricConnector* metric_connector_;

  std::mutex window_init_lock_;  // 多个线程只需要一个线程去初始化即可
  RcuMap<RateLimitWindowKey, RateLimitWindow> rate_limit_window_cache_;
  LruHashMap<RateLimitWindowKey, RateLimitWindow>* rate_limit_window_lru_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_QUOTA_MANAGER_H_
