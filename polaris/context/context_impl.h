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

#ifndef POLARIS_CPP_POLARIS_CONTEXT_CONTEXT_IMPL_H_
#define POLARIS_CPP_POLARIS_CONTEXT_CONTEXT_IMPL_H_

#include <pthread.h>

#include <map>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "cache/cache_manager.h"
#include "cache/cache_persist.h"
#include "cache/rcu_map.h"
#include "cache/rcu_time.h"
#include "cache/rcu_unordered_map.h"
#include "config/seed_server.h"
#include "context/system_variables.h"
#include "engine/engine.h"
#include "model/location.h"
#include "monitor/api_stat_registry.h"
#include "monitor/monitor_reporter.h"
#include "monitor/service_record.h"
#include "plugin/server_connector/server_connector.h"
#include "polaris/context.h"
#include "polaris/defs.h"
#include "quota/quota_manager.h"

namespace polaris {

class Clearable;
class ServiceContext;

// 存储Context启动配置信息
struct ContextConfig {
  uint64_t take_effect_time_;
  uint64_t init_finish_time_;
  std::string config_;
};

class ContextImpl {
 public:
  ContextImpl();

  ~ContextImpl();

  ReturnCode Init(Config* config, Context* context, ContextMode mode);

  StatReporter* GetStatReporter() { return stat_reporter_; }

  AlertReporter* GetAlertReporter() { return alert_reporter_; }

  ServerMetric* GetServerMetric() { return server_metric_.get(); }

  std::shared_ptr<ServiceContext> CreateServiceContext(const ServiceKey& service_key);

  ServiceContext* GetServiceContext(const ServiceKey& service_key);

  RcuUnorderedMap<ServiceKey, ServiceContext>& GetServiceContextMap() { return *service_context_map_; }

  void GetAllServiceContext(std::vector<std::shared_ptr<ServiceContext>>& all_service_contexts);

  uint64_t GetApiDefaultTimeout() const { return api_default_timeout_; }

  uint64_t GetApiMaxRetryTimes() const { return max_retry_times_; }

  uint64_t GetApiRetryInterval() const { return retry_interval_; }

  const std::string& GetApiBindIp() const { return sdk_token_.ip(); }

  uint64_t GetReportClientInterval() const { return report_client_interval_; }

  uint64_t GetCacheClearTime() const { return cache_clear_time_; }

  SeedServerConfig& GetSeedConfig() { return seed_config_; }

  ServerConnector* GetServerConnector() const { return server_connector_; }

  const PolarisCluster& GetDiscoverService() const { return seed_config_.discover_cluster_; }

  const PolarisCluster& GetHeartbeatService() const { return seed_config_.heartbeat_cluster_; }

  const PolarisCluster& GetMonitorService() const { return seed_config_.monitor_cluster_; }

  const PolarisCluster& GetMetricService() const { return seed_config_.metric_cluster_; }

  ClientLocation& GetClientLocation() { return client_location_; }

  ApiStatRegistry* GetApiStatRegistry() { return api_stat_registry_; }

  MonitorReporter* GetMonitorReporter() { return engine_->GetMonitorReporter(); }

  ServiceRecord* GetServiceRecord() { return service_record_; }

  CacheManager* GetCacheManager() { return engine_->GetCacheManager(); }

  CircuitBreakerExecutor* GetCircuitBreakerExecutor() { return engine_->GetCircuitBreakerExecutor(); }

  QuotaManager* GetQuotaManager() { return quota_manager_; }

  const v1::SDKToken& GetSdkToken() { return sdk_token_; }

  const ContextConfig& GetContextConfig() { return context_config_; }

  const SystemVariables& GetSystemVariables() const { return system_variables_; }

  void RegisterCache(Clearable* cache);

  void ClearCache();

  void RcuEnter() { thread_time_mgr_->RcuEnter(); }

  void RcuExit() { thread_time_mgr_->RcuExit(); }

  uint64_t RcuMinTime() { return thread_time_mgr_->MinTime(); }

  int GetCreateForkCount() const { return create_at_fork_count_; }

 private:
  ReturnCode InitGlobalConfig(Config* global_config, Context* context);

  // 初始化System级别的配置项
  ReturnCode InitSystemConfig(Config* system_config);

  ReturnCode InitSystemService(const PolarisCluster& cluster);

  // 初始化API级别的配置项
  ReturnCode InitApiConfig(Config* api_config);

  ReturnCode InitConsumerConfig(Config* consumer_config, Context* context);

  ReturnCode VerifyServiceConfig(Config* config);

 private:
  friend class Context;
  friend class TestContext;
  ContextMode context_mode_;
  Context* context_;

  ContextConfig context_config_;

  // API 超时及重试配置
  uint64_t api_default_timeout_;
  uint64_t max_retry_times_;
  uint64_t retry_interval_;

  // SDK上报用于获取位置信息
  v1::SDKToken sdk_token_;
  uint64_t report_client_interval_;  // TODO 待确定范围
  ClientLocation client_location_;
  uint64_t cache_clear_time_;

  SeedServerConfig seed_config_;
  SystemVariables system_variables_;

  // Context level plugins
  ServerConnector* server_connector_;
  LocalRegistry* local_registry_;
  StatReporter* stat_reporter_;
  AlertReporter* alert_reporter_;
  std::unique_ptr<ServerMetric> server_metric_;
  QuotaManager* quota_manager_;  // 配额管理器

  // Service config and Service level context
  Config* global_service_config_;
  std::map<ServiceKey, Config*> service_config_map_;
  std::unique_ptr<RcuUnorderedMap<ServiceKey, ServiceContext>> service_context_map_;

  Engine* engine_;

  ApiStatRegistry* api_stat_registry_;
  ServiceRecord* service_record_;

  ThreadTimeMgr* thread_time_mgr_;
  std::mutex cache_lock_;
  uint64_t last_clear_handler_;
  std::map<uint64_t, Clearable*> cache_map_;

  // 用于检查是否在fork后使用context
  int create_at_fork_count_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CONTEXT_CONTEXT_IMPL_H_
