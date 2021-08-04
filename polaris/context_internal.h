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

#ifndef POLARIS_CPP_POLARIS_CONTEXT_INTERNAL_H_
#define POLARIS_CPP_POLARIS_CONTEXT_INTERNAL_H_

#include <pthread.h>

#include <map>
#include <queue>
#include <string>
#include <vector>

#include "cache/cache_manager.h"
#include "cache/cache_persist.h"
#include "cache/rcu_map.h"
#include "cache/rcu_time.h"
#include "config/seed_server.h"
#include "context/system_variables.h"
#include "engine/engine.h"
#include "model/location.h"
#include "monitor/api_stat_registry.h"
#include "monitor/monitor_reporter.h"
#include "monitor/service_record.h"
#include "plugin/circuit_breaker/circuit_breaker.h"
#include "plugin/circuit_breaker/set_circuit_breaker.h"
#include "plugin/health_checker/health_checker.h"
#include "plugin/service_router/service_router.h"
#include "polaris/context.h"
#include "polaris/defs.h"
#include "quota/quota_manager.h"

namespace polaris {

class Clearable;
class ServiceContextImpl {
public:
  ServiceContextImpl();

  ~ServiceContextImpl();

  ReturnCode Init(const ServiceKey& service_key, Config* config, Context* context);

  void UpdateLastUseTime();

  uint64_t GetLastUseTime() { return last_use_time_; }

private:
  friend class ServiceContext;
  Context* context_;
  ServiceRouterChain* service_router_chain_;
  LoadBalancer* load_balancer_;
  RcuMap<LoadBalanceType, LoadBalancer> lb_map_;
  WeightAdjuster* weight_adjuster_;
  CircuitBreakerChain* circuit_breaker_chain_;
  HealthCheckerChain* health_checker_chain_;
  uint64_t last_use_time_;
};

namespace ApiConfig {
static const char kApiTimeoutKey[]       = "timeout";
static const uint64_t kApiTimeoutDefault = 1000;

static const char kApiMaxRetryTimesKey[]       = "maxRetryTimes";
static const uint64_t kApiMaxRetryTimesDefault = 5;

static const char kApiRetryIntervalKey[]       = "retryInterval";
static const uint64_t kApiRetryIntervalDefault = 100;

static const char kApiBindIfKey[]                  = "bindIf";
static const char kApiBindIpKey[]                  = "bindIP";
static const char kClientReportIntervalKey[]       = "reportInterval";
static const uint64_t kClientReportIntervalDefault = 10 * 60 * 1000;  // 10min

static const char kApiCacheClearTimeKey[]       = "cacheClearTime";
static const uint64_t kApiCacheClearTimeDefault = 60 * 1000;  // 1min

static const char kApiLocationKey[]       = "location";
static const char kApiLocationRegionKey[] = "region";
static const char kApiLocationZoneKey[]   = "zone";
static const char kApiLocationCampusKey[] = "campus";
}  // namespace ApiConfig

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

  ServiceContext* GetOrCreateServiceContext(const ServiceKey& service_key);

  void DeleteServiceContext(const ServiceKey& service_key);

  void GetAllServiceContext(std::vector<ServiceContext*>& all_service_contexts);

  uint64_t GetApiDefaultTimeout() const { return api_default_timeout_; }

  uint64_t GetApiMaxRetryTimes() const { return max_retry_times_; }

  uint64_t GetApiRetryInterval() const { return retry_interval_; }

  const std::string& GetApiBindIf() const { return bind_if_; }

  const std::string& GetApiBindIp() const { return bind_ip_; }

  uint64_t GetReportClientInterval() const { return report_client_interval_; }

  uint64_t GetCacheClearTime() const { return cache_clear_time_; }

  void SetApiBindIp(const std::string& bind_ip) { bind_ip_ = bind_ip; }

  SeedServerConfig& GetSeedConfig() { return seed_config_; }

  const PolarisCluster& GetDiscoverService() const { return seed_config_.discover_cluster_; }

  const PolarisCluster& GetHeartbeatService() const { return seed_config_.heartbeat_cluster_; }

  const PolarisCluster& GetMonitorService() const { return seed_config_.monitor_cluster_; }

  const PolarisCluster& GetMetricService() const { return seed_config_.metric_cluster_; }

  model::ClientLocation& GetClientLocation() { return client_location_; }

  ApiStatRegistry* GetApiStatRegistry() { return api_stat_registry_; }

  MonitorReporter* GetMonitorReporter() { return engine_->GetMonitorReporter(); }

  ServiceRecord* GetServiceRecord() { return service_record_; }

  CacheManager* GetCacheManager() { return engine_->GetCacheManager(); }

  CircuitBreakerExecutor* GetCircuitBreakerExecutor() {
    return engine_->GetCircuitBreakerExecutor();
  }

  QuotaManager* GetQuotaManager() { return quota_manager_; }

  const v1::SDKToken& GetSdkToken() { return sdk_token_; }

  const ContextConfig& GetContextConfig() { return context_config_; }

  const SystemVariables& GetSystemVariables() const { return system_variables_; }

  void RegisterCache(Clearable* cache);

  void ClearCache();

  void RcuEnter() { thread_time_mgr_->RcuEnter(); }

  void RcuExit() { thread_time_mgr_->RcuExit(); }

  uint64_t RcuMinTime() { return thread_time_mgr_->MinTime(); }

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

  v1::SDKToken sdk_token_;
  ContextConfig context_config_;
  ApiMode api_mode_;

  // API 超时及重试配置
  uint64_t api_default_timeout_;
  uint64_t max_retry_times_;
  uint64_t retry_interval_;

  // SDK上报用于获取位置信息
  std::string bind_if_;
  std::string bind_ip_;
  uint64_t report_client_interval_;  // TODO 待确定范围
  model::ClientLocation client_location_;
  uint64_t cache_clear_time_;

  SeedServerConfig seed_config_;
  SystemVariables system_variables_;

  // Context level plugins
  ServerConnector* server_connector_;
  LocalRegistry* local_registry_;
  StatReporter* stat_reporter_;
  AlertReporter* alert_reporter_;
  QuotaManager* quota_manager_;  // 配额管理器

  // Service config and Service level context
  Config* global_service_config_;
  pthread_rwlock_t rwlock_;
  RcuMap<ServiceKey, ServiceContext>* service_context_map_;

  Engine* engine_;

  ApiStatRegistry* api_stat_registry_;
  ServiceRecord* service_record_;

  ThreadTimeMgr* thread_time_mgr_;
  pthread_rwlock_t cache_rwlock_;
  uint64_t last_clear_handler_;
  std::map<uint64_t, Clearable*> cache_map_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CONTEXT_INTERNAL_H_
