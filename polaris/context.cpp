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

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <v1/request.pb.h>

#include <iosfwd>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "cache/rcu_map.h"
#include "cache/rcu_time.h"
#include "cache/service_cache.h"
#include "config/seed_server.h"
#include "context_internal.h"
#include "engine/engine.h"
#include "logger.h"
#include "model/constants.h"
#include "model/location.h"
#include "monitor/api_stat_registry.h"
#include "monitor/service_record.h"
#include "plugin/circuit_breaker/circuit_breaker.h"
#include "plugin/health_checker/health_checker.h"
#include "plugin/plugin_manager.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "quota/quota_manager.h"
#include "utils/netclient.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

ServiceContext::ServiceContext(ServiceContextImpl* impl) { impl_ = impl; }

ServiceContext::~ServiceContext() {
  if (impl_ != NULL) {
    delete impl_;
    impl_ = NULL;
  }
}

ServiceRouterChain* ServiceContext::GetServiceRouterChain() { return impl_->service_router_chain_; }

LoadBalancer* ServiceContext::GetLoadBalancer(LoadBalanceType load_balance_type) {
  if (load_balance_type == kLoadBalanceTypeDefaultConfig) {
    return impl_->load_balancer_;
  } else if (load_balance_type < kLoadBalanceTypeDefaultConfig) {
    LoadBalancer* load_balancer = impl_->lb_table_[load_balance_type];
    if (load_balancer != NULL) {
      return load_balancer;
    }
    Config* config = Config::CreateEmptyConfig();
    Plugin* plugin = NULL;
    PluginManager::Instance().GetLoadBalancePlugin(load_balance_type, plugin);
    load_balancer = dynamic_cast<LoadBalancer*>(plugin);
    POLARIS_ASSERT(load_balancer != NULL);
    ReturnCode ret_code = load_balancer->Init(config, impl_->context_);
    delete config;
    POLARIS_ASSERT(ret_code == kReturnOk);
    if (ATOMIC_CAS(&impl_->lb_table_[load_balance_type], NULL, load_balancer)) {
      return load_balancer;
    } else {
      delete load_balancer;
      return impl_->lb_table_[load_balance_type];
    }
  } else {
    POLARIS_ASSERT(load_balance_type <= kLoadBalanceTypeDefaultConfig);
    return NULL;
  }
}

WeightAdjuster* ServiceContext::GetWeightAdjuster() { return impl_->weight_adjuster_; }

CircuitBreakerChain* ServiceContext::GetCircuitBreakerChain() {
  return impl_->circuit_breaker_chain_;
}

HealthCheckerChain* ServiceContext::GetHealthCheckerChain() {
  return impl_->health_checker_chain_;
}

ServiceContextImpl* ServiceContext::GetServiceContextImpl() { return impl_; }

ServiceContextImpl::ServiceContextImpl() {
  context_              = NULL;
  service_router_chain_ = NULL;
  load_balancer_        = NULL;
  for (int i = 0; i < kLoadBalanceTypeDefaultConfig; ++i) {
    lb_table_[i] = NULL;
  }
  weight_adjuster_        = NULL;
  circuit_breaker_chain_  = NULL;
  health_checker_chain_ = NULL;
  UpdateLastUseTime();
}

ServiceContextImpl::~ServiceContextImpl() {
  context_ = NULL;
  if (service_router_chain_ != NULL) {
    delete service_router_chain_;
    service_router_chain_ = NULL;
  }
  for (int i = 0; i < kLoadBalanceTypeDefaultConfig; ++i) {
    if (lb_table_[i] != NULL) {
      delete lb_table_[i];
    }
  }
  if (weight_adjuster_ != NULL) {
    delete weight_adjuster_;
    weight_adjuster_ = NULL;
  }
  if (circuit_breaker_chain_ != NULL) {
    delete circuit_breaker_chain_;
    circuit_breaker_chain_ = NULL;
  }
  if (health_checker_chain_ != NULL) {
    delete health_checker_chain_;
    health_checker_chain_ = NULL;
  }
}

void ServiceContextImpl::UpdateLastUseTime() { last_use_time_ = Time::GetCurrentTimeMs(); }

ReturnCode ServiceContextImpl::Init(const ServiceKey& service_key, Config* config,
                                    Context* context) {
  context_ = context;
  // 初始化路由插件
  Config* plugin_config = config->GetSubConfig("serviceRouter");
  service_router_chain_ = new ServiceRouterChain(service_key);
  ReturnCode ret        = service_router_chain_->Init(plugin_config, context);
  delete plugin_config;
  if (ret != kReturnOk) {
    return ret;
  }

  // 初始化负载均衡插件
  plugin_config           = config->GetSubConfig("loadBalancer");
  Plugin* plugin          = NULL;
  std::string plugin_name = plugin_config->GetStringOrDefault("type", kPluginDefaultLoadBalancer);
  PluginManager::Instance().GetPlugin(plugin_name, kPluginLoadBalancer, plugin);
  load_balancer_ = dynamic_cast<LoadBalancer*>(plugin);
  if (load_balancer_ == NULL) {
    POLARIS_LOG(LOG_ERROR,
                "Plugin factory register with name[%s] and type[%s] return error "
                "load balancer instance",
                plugin_name.c_str(), PluginTypeToString(kPluginLoadBalancer));
    delete plugin;
    delete plugin_config;
    return kReturnPluginError;
  }
  ret = load_balancer_->Init(plugin_config, context);
  delete plugin_config;
  lb_table_[load_balancer_->GetLoadBalanceType()] = load_balancer_;
  if (ret != kReturnOk) {
    return ret;
  }

  // 初始化探活插件
  plugin_config           = config->GetSubConfig("healthCheck");
  health_checker_chain_ = new HealthCheckerChainImpl(service_key, context->GetLocalRegistry(),
                                                         circuit_breaker_chain_);
  ret                     = health_checker_chain_->Init(plugin_config, context);
  delete plugin_config;
  if (ret != kReturnOk) {
    return ret;
  }

  // 初始化熔断插件
  plugin_config          = config->GetSubConfig("circuitBreaker");
  circuit_breaker_chain_ = new CircuitBreakerChainImpl(
      service_key, context->GetLocalRegistry(),
      health_checker_chain_->GetHealthCheckers().empty());  // 探测插件为空，则表示开启自动半开
  ret = circuit_breaker_chain_->Init(plugin_config, context);
  delete plugin_config;
  if (ret != kReturnOk) {
    return ret;
  }

  // 初始化动态权重调整插件
  plugin_config = config->GetSubConfig("weightAdjuster");
  plugin        = NULL;
  plugin_name   = plugin_config->GetStringOrDefault("name", kPluginDefaultWeightAdjuster);
  PluginManager::Instance().GetPlugin(plugin_name, kPluginWeightAdjuster, plugin);
  weight_adjuster_ = dynamic_cast<WeightAdjuster*>(plugin);
  if (weight_adjuster_ == NULL) {
    POLARIS_LOG(LOG_ERROR,
                "Plugin factory register with name[%s] and type[%s] return "
                "error weight adjuster instance",
                plugin_name.c_str(), PluginTypeToString(kPluginWeightAdjuster));
    delete plugin;
    delete plugin_config;
    return kReturnPluginError;
  }
  ret = weight_adjuster_->Init(plugin_config, context);
  delete plugin_config;
  if (ret != kReturnOk) {
    return ret;
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////////
Context::Context(ContextImpl* impl) { impl_ = impl; }

Context::~Context() {
  if (impl_ != NULL) {
    delete impl_;
    impl_ = NULL;
  }
}

/// @brief 从配置中初始化运行上下文
Context* Context::Create(Config* config, ContextMode mode) {
  if (config == NULL) {
    POLARIS_LOG(LOG_WARN, "create context failed because parameter config is null");
    return NULL;
  }
  if (mode <= kNotInitContext) {
    POLARIS_LOG(LOG_WARN, "create context failed because parameter mode is NotInitContext");
    return NULL;
  }

  ContextImpl* context_impl = new ContextImpl();
  Context* context          = new Context(context_impl);
  if (context_impl->Init(config, context, mode) != kReturnOk) {
    delete context;
    return NULL;
  }
  // Polaris discover先请求一下
  if (context_impl->InitSystemService(context_impl->GetDiscoverService()) != kReturnOk) {
    delete context;
    return NULL;
  }
  // 如果有设置Metric Cluster则提前获取
  const PolarisCluster& metric_cluster = context_impl->GetMetricService();
  if (!metric_cluster.service_.name_.empty() &&
      context_impl->InitSystemService(metric_cluster) != kReturnOk) {
    delete context;
    return NULL;
  }
  return context;
}

ContextMode Context::GetContextMode() { return impl_->context_mode_; }

ApiMode Context::GetApiMode() { return impl_->api_mode_; }

ServerConnector* Context::GetServerConnector() { return impl_->server_connector_; }

LocalRegistry* Context::GetLocalRegistry() { return impl_->local_registry_; }

ServiceContext* Context::GetOrCreateServiceContext(const ServiceKey& service_key) {
  return impl_->GetOrCreateServiceContext(service_key);
}

ContextImpl* Context::GetContextImpl() { return impl_; }

///////////////////////////////////////////////////////////////////////////////
ContextImpl::ContextImpl() {
  context_mode_           = kNotInitContext;
  api_mode_               = kServerApiMode;
  api_default_timeout_    = 0;
  max_retry_times_        = 0;
  retry_interval_         = 0;
  report_client_interval_ = 0;
  cache_clear_time_       = 0;

  server_connector_ = NULL;
  local_registry_   = NULL;
  stat_reporter_    = NULL;
  alert_reporter_   = NULL;
  quota_manager_    = NULL;

  global_service_config_ = NULL;
  pthread_rwlock_init(&rwlock_, NULL);
  service_context_map_ = new RcuMap<ServiceKey, ServiceContext>();

  api_stat_registry_ = NULL;
  service_record_    = NULL;
  engine_            = NULL;
  context_           = NULL;
  pthread_rwlock_init(&cache_rwlock_, NULL);
  last_clear_handler_ = 1;
  thread_time_mgr_    = new ThreadTimeMgr();

  Time::TrySetUpClock();
}

ContextImpl::~ContextImpl() {
  if (engine_ != NULL) {
    engine_->StopAndWait();
  }
  if (server_connector_ != NULL) {
    delete server_connector_;
    server_connector_ = NULL;
  }
  if (quota_manager_ != NULL) {
    delete quota_manager_;
    quota_manager_ = NULL;
  }
  context_ = NULL;
  if (engine_ != NULL) {
    // 必须在所有进程停止以后删除engine，防止其他进程访问其中的cache_manager
    // TODO 将server connector线程和quota manager线程统一到engine管理
    delete engine_;
    engine_ = NULL;
  }
  if (api_stat_registry_ != NULL) {
    delete api_stat_registry_;
    api_stat_registry_ = NULL;
  }
  if (service_record_ != NULL) {
    delete service_record_;
    service_record_ = NULL;
  }
  // 服务级别有缓存数据，必须在LocalRegistry前先释放
  if (service_context_map_ != NULL) {
    delete service_context_map_;
    service_context_map_ = NULL;
  }
  pthread_rwlock_destroy(&cache_rwlock_);
  if (thread_time_mgr_ != NULL) {
    delete thread_time_mgr_;
    thread_time_mgr_ = NULL;
  }
  for (std::map<uint64_t, Clearable*>::iterator it = cache_map_.begin(); it != cache_map_.end();
       ++it) {
    it->second->DecrementRef();
  }
  if (local_registry_ != NULL) {
    delete local_registry_;
    local_registry_ = NULL;
  }
  if (stat_reporter_ != NULL) {
    delete stat_reporter_;
    stat_reporter_ = NULL;
  }
  if (alert_reporter_ != NULL) {
    delete alert_reporter_;
    alert_reporter_ = NULL;
  }

  if (global_service_config_ != NULL) {
    delete global_service_config_;
    global_service_config_ = NULL;
  }
  pthread_rwlock_destroy(&rwlock_);

  Time::TryShutdomClock();
}

static const char kInnerServiceConfig[] =
    "serviceRouter:\n"
    "  chain: [dstMetaRouter, nearbyBasedRouter]\n"
    "  plugin:\n"
    "    nearbyBasedRouter:\n"
    "      matchLevel: region\n"
    "circuitBreaker:\n"
    "  plugin:\n"
    "    errorCount:\n"
    "      continuousErrorThreshold: 1\n"
    "      requestCountAfterHalfOpen: 3\n"
    "      successCountAfterHalfOpen: 2";

ServiceContext* ContextImpl::GetOrCreateServiceContext(const ServiceKey& service_key) {
  ServiceContext* service_context = service_context_map_->Get(service_key);
  if (service_context != NULL) {
    return service_context;
  }

  // 读取失败升级成写锁再尝试读，读取失败则创建并写入
  pthread_rwlock_wrlock(&rwlock_);
  service_context = service_context_map_->Get(service_key);
  if (service_context == NULL) {
    ServiceContextImpl* service_context_impl = new ServiceContextImpl();
    ReturnCode ret                           = kReturnOk;
    if (service_key.namespace_ == constants::kPolarisNamespace) {
      // Polaris命名空间的服务不受业务配置影响
      std::string err_msg;
      Config* inner_service_config = Config::CreateFromString(kInnerServiceConfig, err_msg);
      POLARIS_ASSERT(inner_service_config != NULL);
      ret = service_context_impl->Init(service_key, inner_service_config, context_);
      delete inner_service_config;
    } else {
      ret = service_context_impl->Init(service_key, global_service_config_, context_);
    }
    if (ret != kReturnOk) {
      POLARIS_LOG(LOG_ERROR, "create context for service[%s/%s] failed",
                  service_key.namespace_.c_str(), service_key.name_.c_str());
      delete service_context_impl;
    } else {
      service_context = new ServiceContext(service_context_impl);
      service_context_map_->Update(service_key, service_context);
      service_context->IncrementRef();
    }
  }
  pthread_rwlock_unlock(&rwlock_);
  return service_context;
}

void ContextImpl::DeleteServiceContext(const ServiceKey& service_key) {
  service_context_map_->Delete(service_key);
}

void ContextImpl::GetAllServiceContext(std::vector<ServiceContext*>& all_service_contexts) {
  service_context_map_->GetAllValuesWithRef(all_service_contexts);
}

void PolarisClusterDecode(Config* config, const std::string& cluster_key, PolarisCluster& cluster) {
  static const char kPolarisNamespaceKey[]       = "namespace";
  static const char kPolarisServiceKey[]         = "service";
  static const char kPolarisRefreshIntervalKey[] = "refreshInterval";

  Config* cluster_config = config->GetSubConfig(cluster_key);
  cluster.service_.namespace_ =
      cluster_config->GetStringOrDefault(kPolarisNamespaceKey, cluster.service_.namespace_);
  cluster.service_.name_ =
      cluster_config->GetStringOrDefault(kPolarisServiceKey, cluster.service_.name_);
  cluster.refresh_interval_ =
      cluster_config->GetMsOrDefault(kPolarisRefreshIntervalKey, cluster.refresh_interval_);
  delete cluster_config;
}

ReturnCode ContextImpl::InitSystemConfig(Config* system_config) {
  static const char kModeKey[] = "mode";
  int api_mode_value           = system_config->GetIntOrDefault(kModeKey, 0);
  if (api_mode_value == 0) {
    api_mode_ = kServerApiMode;
  } else if (api_mode_value == 1) {
    api_mode_ = kAgentApiMode;
  } else {
    POLARIS_LOG(LOG_ERROR, "config error: api mode must be 0 or 1");
    delete system_config;
    return kReturnInvalidConfig;
  }

  static const char kDiscoverClusterKey[]    = "discoverCluster";
  static const char kHealthCheckClusterKey[] = "healthCheckCluster";
  static const char kMonitorClusterKey[]     = "monitorCluster";
  static const char kMetricClusterKey[]      = "metricCluster";
  // 内置服务
  PolarisClusterDecode(system_config, kDiscoverClusterKey, seed_config_.discover_cluster_);
  PolarisClusterDecode(system_config, kHealthCheckClusterKey, seed_config_.heartbeat_cluster_);
  PolarisClusterDecode(system_config, kMonitorClusterKey, seed_config_.monitor_cluster_);
  PolarisClusterDecode(system_config, kMetricClusterKey, seed_config_.metric_cluster_);

  std::map<std::string, std::string> config_variables = system_config->GetMap("variables");
  system_variables_.InitFromConfig(config_variables);

  delete system_config;
  return kReturnOk;
}

ReturnCode ContextImpl::InitSystemService(const PolarisCluster& cluster) {
  ServiceContext* service_context = this->GetOrCreateServiceContext(cluster.service_);
  if (service_context == NULL) {
    POLARIS_LOG(LOG_ERROR, "create service context for service[%s/%s] failed",
                cluster.service_.namespace_.c_str(), cluster.service_.name_.c_str());
    return kReturnInvalidConfig;
  }
  ServiceRouterChain* router_chain = service_context->GetServiceRouterChain();
  POLARIS_ASSERT(router_chain != NULL);
  RouteInfo route_info(cluster.service_, NULL);
  RouteInfoNotify* route_info_notify = router_chain->PrepareRouteInfoWithNotify(route_info);
  if (route_info_notify != NULL) {
    delete route_info_notify;
  }
  service_context->DecrementRef();
  return kReturnOk;
}

extern const char* g_sdk_type;
extern const char* g_sdk_version;

ReturnCode ContextImpl::InitApiConfig(Config* api_config) {
  api_default_timeout_ =
      api_config->GetMsOrDefault(ApiConfig::kApiTimeoutKey, ApiConfig::kApiTimeoutDefault);
  if (api_default_timeout_ < 1) {
    POLARIS_LOG(LOG_ERROR, "api %s must equal or great than 1 ms", ApiConfig::kApiTimeoutKey);
    return kReturnInvalidConfig;
  }
  max_retry_times_ = api_config->GetIntOrDefault(ApiConfig::kApiMaxRetryTimesKey,
                                                 ApiConfig::kApiMaxRetryTimesDefault);
  retry_interval_  = api_config->GetMsOrDefault(ApiConfig::kApiRetryIntervalKey,
                                               ApiConfig::kApiRetryIntervalDefault);
  if (retry_interval_ < 10) {
    POLARIS_LOG(LOG_ERROR, "api %s must equal or great than 10ms", ApiConfig::kApiRetryIntervalKey);
    return kReturnInvalidConfig;
  }
  bind_if_ = api_config->GetStringOrDefault(ApiConfig::kApiBindIfKey, "");
  bind_ip_ = api_config->GetStringOrDefault(ApiConfig::kApiBindIpKey, "");
  if (bind_ip_.empty()) {  // 没有配置bind ip， 获取本机IP
    // bind_if不为空，则通过bind_if获取
    if (!bind_if_.empty()) {
      if (!NetClient::GetIpByIf(bind_if_, &bind_ip_)) {
        POLARIS_LOG(LOG_ERROR, "get local ip address by bindIf: %s failed", bind_if_.c_str());
        return kReturnInvalidConfig;
      } else {
        POLARIS_LOG(LOG_INFO, "get local ip address by bindIf:%s return ip:%s", bind_if_.c_str(),
                    bind_ip_.c_str());
      }
    } else if (!NetClient::GetIpByConnect(&bind_ip_)) {
      return kReturnInvalidConfig;
    } else {
      POLARIS_LOG(LOG_INFO, "get local ip address by connection return ip:%s", bind_ip_.c_str());
    }
  }
  POLARIS_ASSERT(!bind_ip_.empty());
  sdk_token_.set_ip(bind_ip_);
  sdk_token_.set_pid(getpid());
  sdk_token_.set_uid(Utils::Uuid());
  sdk_token_.set_client(g_sdk_type);
  sdk_token_.set_version(g_sdk_version);

  static const char kTkePodName[]      = "POD_NAME";         // TKE
  static const char kSumeruPodName[]   = "SUMERU_POD_NAME";  // 123
  static const char kTkeStackPodName[] = "MY_POD_NAME";      // tke stack
  char* pod_name_env                   = getenv(kTkePodName);
  if (pod_name_env != NULL || (pod_name_env = getenv(kSumeruPodName)) != NULL ||
      (pod_name_env = getenv(kTkeStackPodName)) != NULL) {
    sdk_token_.set_pod_name(pod_name_env);
  }
  char* host_name_env = getenv("HOSTNAME");
  if (host_name_env != NULL) {
    sdk_token_.set_host_name(host_name_env);
  }

  report_client_interval_ = api_config->GetMsOrDefault(ApiConfig::kClientReportIntervalKey,
                                                       ApiConfig::kClientReportIntervalDefault);
  if (report_client_interval_ < 10 * 1000) {
    POLARIS_LOG(LOG_ERROR, "api %s must equal or great than 10s",
                ApiConfig::kClientReportIntervalKey);
    return kReturnInvalidConfig;
  }
  cache_clear_time_ = api_config->GetMsOrDefault(ApiConfig::kApiCacheClearTimeKey,
                                                 ApiConfig::kApiCacheClearTimeDefault);
  if (cache_clear_time_ < 1000) {
    POLARIS_LOG(LOG_ERROR, "api %s must equal or great than 1s", ApiConfig::kApiCacheClearTimeKey);
    return kReturnInvalidConfig;
  }

  // 客户端地域信息
  Config* location_config = api_config->GetSubConfig(ApiConfig::kApiLocationKey);
  Location location;
  location.region = location_config->GetStringOrDefault(ApiConfig::kApiLocationRegionKey, "");
  location.zone   = location_config->GetStringOrDefault(ApiConfig::kApiLocationZoneKey, "");
  location.campus = location_config->GetStringOrDefault(ApiConfig::kApiLocationCampusKey, "");
  client_location_.Init(location);
  delete location_config;
  return kReturnOk;
}

ReturnCode ContextImpl::InitGlobalConfig(Config* config, Context* context) {
  // Init server connector plugin
  Config* plugin_config = config->GetSubConfig("serverConnector");
  Plugin* plugin        = NULL;
  std::string protocol =
      plugin_config->GetStringOrDefault("protocol", kPluginDefaultServerConnector);
  PluginManager::Instance().GetPlugin(protocol, kPluginServerConnector, plugin);
  server_connector_ = dynamic_cast<ServerConnector*>(plugin);
  if (server_connector_ == NULL) {
    POLARIS_LOG(LOG_ERROR,
                "Plugin factory register with name[%s] and type[%s] return error instance",
                protocol.c_str(), PluginTypeToString(kPluginServerConnector));
    delete plugin_config;
    return kReturnPluginError;
  }
  ReturnCode ret = server_connector_->Init(plugin_config, context);
  delete plugin_config;
  if (ret != kReturnOk) {
    return ret;
  }

  // 必须放到server connector之后初始化，保证server connector里的join point已经读取
  if ((ret = InitSystemConfig(config->GetSubConfig("system"))) != kReturnOk) {
    return ret;
  }

  // init api mode and api default timeout
  {
    Config* api_config = config->GetSubConfig("api");
    ret                = InitApiConfig(api_config);
    delete api_config;
    if (ret != kReturnOk) {
      return ret;
    }
  }

  // Init stat reporter
  plugin_config           = config->GetSubConfig("statReporter");
  plugin                  = NULL;
  std::string plugin_name = plugin_config->GetStringOrDefault("name", kPluginDefaultStatReporter);
  PluginManager::Instance().GetPlugin(plugin_name, kPluginStatReporter, plugin);
  stat_reporter_ = dynamic_cast<StatReporter*>(plugin);
  if (stat_reporter_ == NULL) {
    POLARIS_LOG(LOG_ERROR,
                "Plugin factory register with name[%s] and type[%s] return error instance",
                plugin_name.c_str(), PluginTypeToString(kPluginStatReporter));
    delete plugin_config;
    return kReturnPluginError;
  }
  ret = stat_reporter_->Init(plugin_config, context);
  delete plugin_config;
  if (ret != kReturnOk) {
    return ret;
  }

  // Init alert reporter
  plugin_config = config->GetSubConfig("alertReporter");
  plugin        = NULL;
  plugin_name   = plugin_config->GetStringOrDefault("name", kPluginDefaultAlertReporter);
  PluginManager::Instance().GetPlugin(plugin_name, kPluginAlertReporter, plugin);
  alert_reporter_ = dynamic_cast<AlertReporter*>(plugin);
  if (alert_reporter_ == NULL) {
    POLARIS_LOG(LOG_ERROR,
                "Plugin factory register with name[%s] and type[%s] return error instance",
                plugin_name.c_str(), PluginTypeToString(kPluginAlertReporter));
    delete plugin_config;
    return kReturnPluginError;
  }
  ret = alert_reporter_->Init(plugin_config, context);
  delete plugin_config;
  return ret;
}

ReturnCode ContextImpl::InitConsumerConfig(Config* consumer_config, Context* context) {
  // Init local registry
  Config* plugin_config   = consumer_config->GetSubConfig("localCache");
  Plugin* plugin          = NULL;
  std::string plugin_name = plugin_config->GetStringOrDefault("type", kPluginDefaultLocalRegistry);
  PluginManager::Instance().GetPlugin(plugin_name, kPluginLocalRegistry, plugin);
  local_registry_ = dynamic_cast<LocalRegistry*>(plugin);
  if (local_registry_ == NULL) {
    POLARIS_LOG(LOG_ERROR,
                "Plugin factory register with name[%s] and type[%s] return error instance",
                plugin_name.c_str(), PluginTypeToString(kPluginLocalRegistry));
    delete plugin_config;
    return kReturnPluginError;
  }
  ReturnCode ret = local_registry_->Init(plugin_config, context);
  delete plugin_config;
  return ret;
}

ReturnCode ContextImpl::VerifyServiceConfig(Config* config) {
  // 等待位置信息
  client_location_.WaitInit(api_default_timeout_);
  // 创建一个服务上下文用来校验配置
  ServiceKey verify_service_key  = {"polaris_cpp", "verify_default_config"};
  global_service_config_         = config;
  ServiceContext* verify_context = this->GetOrCreateServiceContext(verify_service_key);
  global_service_config_         = NULL;
  if (verify_context == NULL) {
    return kReturnInvalidConfig;
  }
  verify_context->DecrementRef();
  this->DeleteServiceContext(verify_service_key);
  return kReturnOk;
}

ReturnCode ContextImpl::Init(Config* config, Context* context, ContextMode mode) {
  context_mode_ = mode;
  context_      = context;
  ReturnCode ret;
  context_config_.take_effect_time_ = Time::GetCurrentTimeMs();
  {
    Config* global_config = config->GetSubConfig("global");
    ReturnCode ret        = InitGlobalConfig(global_config, context);
    delete global_config;
    if (ret != kReturnOk) {
      return ret;
    }
  }
  // 必须先创建ServiceRecord对象
  service_record_    = new ServiceRecord();
  api_stat_registry_ = new ApiStatRegistry(context_);
  engine_            = new Engine(context_);

  Config* consumer_config = config->GetSubConfig("consumer");
  if ((ret = InitConsumerConfig(consumer_config, context)) != kReturnOk) {
    delete consumer_config;
    return ret;
  }
  quota_manager_ = new QuotaManager();  // 启动线程前需要先创建对象

  // 创建任务流和构建执行引擎
  if (mode != kShareContextWithoutEngine) {  // 不启动线程模式用于测试
    if ((ret = engine_->Start()) != kReturnOk) {
      delete consumer_config;
      return ret;
    }
  }

  // 校验服务级别插件配置
  ret = VerifyServiceConfig(consumer_config);
  delete consumer_config;
  if (ret != kReturnOk) {
    return ret;
  }
  global_service_config_ = config->GetSubConfigClone("consumer");

  {  // 初始化QuotaManager
    Config* rate_limit_config = config->GetSubConfig("rateLimiter");
    ret                       = quota_manager_->Init(context_, rate_limit_config);
    delete rate_limit_config;
    if (ret != kReturnOk) {
      return ret;
    }
  }
  context_config_.init_finish_time_ = Time::GetCurrentTimeMs();
  context_config_.config_           = config->ToJsonString();
  POLARIS_LOG(LOG_INFO, "===== create context[%s] with config:\n%s\n =====",
              sdk_token_.ShortDebugString().c_str(), config->ToString().c_str());
  return ret;
}

void ContextImpl::RegisterCache(Clearable* cache) {
  cache->IncrementRef();
  pthread_rwlock_wrlock(&cache_rwlock_);
  last_clear_handler_++;
  cache->SetClearHandler(last_clear_handler_);
  cache_map_.insert(std::make_pair(last_clear_handler_, cache));
  pthread_rwlock_unlock(&cache_rwlock_);
}

void ContextImpl::ClearCache() {
  std::vector<uint64_t> delete_cache_;
  std::vector<uint64_t> clear_cache_;
  pthread_rwlock_rdlock(&cache_rwlock_);
  for (std::map<uint64_t, Clearable*>::iterator it = cache_map_.begin(); it != cache_map_.end();
       ++it) {
    if (it->second->GetClearHandler() == it->first) {
      clear_cache_.push_back(it->first);
    } else {
      delete_cache_.push_back(it->first);
    }
  }
  // 先删除无效cache
  pthread_rwlock_unlock(&cache_rwlock_);
  std::map<uint64_t, Clearable*>::iterator it;
  for (std::size_t i = 0; i < delete_cache_.size(); i++) {
    Clearable* clearable = NULL;
    pthread_rwlock_wrlock(&cache_rwlock_);
    it = cache_map_.find(delete_cache_[i]);
    if (it != cache_map_.end()) {
      clearable = it->second;
      cache_map_.erase(it);
    }
    pthread_rwlock_unlock(&cache_rwlock_);
    if (clearable != NULL) {
      clearable->DecrementRef();
    }
  }
  // 清理还在使用的cache
  uint64_t min_access_time = thread_time_mgr_->MinTime() - cache_clear_time_;  // 1s没有访问就清除
  for (std::size_t i = 0; i < clear_cache_.size(); i++) {
    pthread_rwlock_rdlock(&cache_rwlock_);
    it = cache_map_.find(clear_cache_[i]);
    if (it != cache_map_.end()) {
      it->second->Clear(min_access_time);
    }
    pthread_rwlock_unlock(&cache_rwlock_);
  }
}

}  // namespace polaris
