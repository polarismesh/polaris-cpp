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

#include "context/context_impl.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <v1/request.pb.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cache/rcu_map.h"
#include "cache/rcu_time.h"
#include "cache/service_cache.h"
#include "config/seed_server.h"
#include "context/service_context.h"
#include "engine/engine.h"
#include "logger.h"
#include "model/constants.h"
#include "model/location.h"
#include "monitor/api_stat_registry.h"
#include "monitor/service_record.h"
#include "plugin/plugin_manager.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "quota/quota_manager.h"
#include "utils/fork.h"
#include "utils/netclient.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

ContextImpl::ContextImpl() : service_context_map_(new RcuUnorderedMap<ServiceKey, ServiceContext>()) {
  context_mode_ = kNotInitContext;
  api_default_timeout_ = 0;
  max_retry_times_ = 0;
  retry_interval_ = 0;
  report_client_interval_ = 0;
  cache_clear_time_ = 0;

  server_connector_ = nullptr;
  local_registry_ = nullptr;
  stat_reporter_ = nullptr;
  alert_reporter_ = nullptr;
  quota_manager_ = nullptr;

  global_service_config_ = nullptr;

  api_stat_registry_ = nullptr;
  service_record_ = nullptr;
  engine_ = nullptr;
  context_ = nullptr;
  last_clear_handler_ = 1;
  thread_time_mgr_ = new ThreadTimeMgr();

  create_at_fork_count_ = polaris_fork_count;

  Time::TrySetUpClock();
}

ContextImpl::~ContextImpl() {
  if (engine_ != nullptr) {
    engine_->StopAndWait();
  }
  if (server_connector_ != nullptr) {
    delete server_connector_;
    server_connector_ = nullptr;
  }
  if (quota_manager_ != nullptr) {
    delete quota_manager_;
    quota_manager_ = nullptr;
  }

  context_ = nullptr;
  if (engine_ != nullptr) {
    // 必须在所有进程停止以后删除engine，防止其他进程访问其中的cache_manager
    // TODO 将server connector线程和quota manager线程统一到engine管理
    delete engine_;
    engine_ = nullptr;
  }
  if (api_stat_registry_ != nullptr) {
    delete api_stat_registry_;
    api_stat_registry_ = nullptr;
  }
  if (service_record_ != nullptr) {
    delete service_record_;
    service_record_ = nullptr;
  }
  // 服务级别有缓存数据，必须在LocalRegistry前先释放
  service_context_map_.reset();
  if (thread_time_mgr_ != nullptr) {
    delete thread_time_mgr_;
    thread_time_mgr_ = nullptr;
  }
  for (std::map<uint64_t, Clearable*>::iterator it = cache_map_.begin(); it != cache_map_.end(); ++it) {
    it->second->DecrementRef();
  }
  if (local_registry_ != nullptr) {
    delete local_registry_;
    local_registry_ = nullptr;
  }
  if (stat_reporter_ != nullptr) {
    delete stat_reporter_;
    stat_reporter_ = nullptr;
  }
  if (alert_reporter_ != nullptr) {
    delete alert_reporter_;
    alert_reporter_ = nullptr;
  }

  if (global_service_config_ != nullptr) {
    delete global_service_config_;
    global_service_config_ = nullptr;
  }
  for (auto item : service_config_map_) {
    delete item.second;
  }
  service_config_map_.clear();

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

std::shared_ptr<ServiceContext> ContextImpl::CreateServiceContext(const ServiceKey& service_key) {
  std::shared_ptr<ServiceContext> service_context(new ServiceContext());
  ReturnCode ret = kReturnOk;
  if (service_key.namespace_ == constants::kPolarisNamespace) {
    // Polaris命名空间的服务不受业务配置影响
    std::string err_msg;
    std::unique_ptr<Config> inner_service_config(Config::CreateFromString(kInnerServiceConfig, err_msg));
    std::unique_ptr<Config> global_empty_config(Config::CreateEmptyConfig());
    if (!err_msg.empty()) {
      POLARIS_LOG(LOG_ERROR, "create context for service[%s/%s] with error: %s", service_key.namespace_.c_str(),
                  service_key.name_.c_str(), err_msg.c_str());
    }
    POLARIS_ASSERT(inner_service_config != nullptr);
    ret = service_context->Init(service_key, inner_service_config.get(), global_empty_config.get(), context_);
  } else {
    Config* service_config = global_service_config_;
    auto service_config_it = service_config_map_.find(service_key);
    if (service_config_it != service_config_map_.end()) {
      service_config = service_config_it->second;
    }
    ret = service_context->Init(service_key, service_config, global_service_config_, context_);
  }
  if (ret != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "create context for service[%s/%s] failed", service_key.namespace_.c_str(),
                service_key.name_.c_str());
    service_context.reset();
  }
  return service_context;
}

ServiceContext* ContextImpl::GetServiceContext(const ServiceKey& service_key) {
  ServiceContext* service_context = service_context_map_->GetWithRcuTime(service_key);
  if (service_context != nullptr) {
    return service_context;
  }
  std::shared_ptr<ServiceContext> new_service_context =
      service_context_map_->CreateOrGet(service_key, [=] { return CreateServiceContext(service_key); });
  return new_service_context.get();
}

void ContextImpl::GetAllServiceContext(std::vector<std::shared_ptr<ServiceContext>>& all_service_contexts) {
  service_context_map_->GetAllValues(all_service_contexts);
}

void PolarisClusterDecode(Config* config, const std::string& cluster_key, PolarisCluster& cluster) {
  static const char kPolarisNamespaceKey[] = "namespace";
  static const char kPolarisServiceKey[] = "service";
  static const char kPolarisRefreshIntervalKey[] = "refreshInterval";

  Config* cluster_config = config->GetSubConfig(cluster_key);
  cluster.service_.namespace_ = cluster_config->GetStringOrDefault(kPolarisNamespaceKey, cluster.service_.namespace_);
  cluster.service_.name_ = cluster_config->GetStringOrDefault(kPolarisServiceKey, cluster.service_.name_);
  cluster.refresh_interval_ = cluster_config->GetMsOrDefault(kPolarisRefreshIntervalKey, cluster.refresh_interval_);
  delete cluster_config;
}

ReturnCode ContextImpl::InitSystemConfig(Config* system_config) {
  static const char kDiscoverClusterKey[] = "discoverCluster";
  static const char kHealthCheckClusterKey[] = "healthCheckCluster";
  static const char kMonitorClusterKey[] = "monitorCluster";
  static const char kMetricClusterKey[] = "metricCluster";

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
  ServiceContext* service_context = this->GetServiceContext(cluster.service_);
  POLARIS_LOG(LOG_ERROR, "init system service for service[%s/%s] failed", cluster.service_.namespace_.c_str(),
                cluster.service_.name_.c_str());
  if (service_context == nullptr) {
    POLARIS_LOG(LOG_ERROR, "create service context for service[%s/%s] failed", cluster.service_.namespace_.c_str(),
                cluster.service_.name_.c_str());
    return kReturnInvalidConfig;
  }
  ServiceRouterChain* router_chain = service_context->GetServiceRouterChain();
  POLARIS_ASSERT(router_chain != nullptr);
  RouteInfo route_info(cluster.service_, nullptr);
  RouteInfoNotify* route_info_notify = router_chain->PrepareRouteInfoWithNotify(route_info);
  if (route_info_notify != nullptr) {
    delete route_info_notify;
  }
  return kReturnOk;
}

extern const char* g_sdk_type;
extern const char* g_sdk_version;

ReturnCode ContextImpl::InitApiConfig(Config* api_config) {
  api_default_timeout_ = api_config->GetMsOrDefault(constants::kApiTimeoutKey, constants::kApiTimeoutDefault);
  if (api_default_timeout_ < 1) {
    POLARIS_LOG(LOG_ERROR, "api %s must equal or great than 1 ms", constants::kApiTimeoutKey);
    return kReturnInvalidConfig;
  }
  max_retry_times_ = api_config->GetIntOrDefault(constants::kApiMaxRetryTimesKey, constants::kApiMaxRetryTimesDefault);
  retry_interval_ = api_config->GetMsOrDefault(constants::kApiRetryIntervalKey, constants::kApiRetryIntervalDefault);
  if (retry_interval_ < 10) {
    POLARIS_LOG(LOG_ERROR, "api %s must equal or great than 10ms", constants::kApiRetryIntervalKey);
    return kReturnInvalidConfig;
  }
  std::string bind_if = api_config->GetStringOrDefault(constants::kApiBindIfKey, "");
  std::string bind_ip = api_config->GetStringOrDefault(constants::kApiBindIpKey, "");
  if (bind_ip.empty()) {  // 没有配置bind ip， 获取本机IP
    // bind_if不为空，则通过bind_if获取
    if (!bind_if.empty()) {
      if (!NetClient::GetIpByIf(bind_if, &bind_ip)) {
        POLARIS_LOG(LOG_ERROR, "get local ip address by bindIf: %s failed", bind_if.c_str());
        return kReturnInvalidConfig;
      } else {
        POLARIS_LOG(LOG_INFO, "get local ip address by bindIf:%s return ip:%s", bind_if.c_str(), bind_ip.c_str());
      }
    } 
  }
  sdk_token_.set_ip(bind_ip);
  sdk_token_.set_pid(getpid());
  sdk_token_.set_uid(Utils::Uuid());
  sdk_token_.set_client(g_sdk_type);
  sdk_token_.set_version(g_sdk_version);

  static const char kTkePodName[] = "POD_NAME";            // TKE
  static const char kSumeruPodName[] = "SUMERU_POD_NAME";  // 123
  static const char kTkeStackPodName[] = "MY_POD_NAME";    // tke stack
  char* pod_name_env = getenv(kTkePodName);
  if (pod_name_env != nullptr || (pod_name_env = getenv(kSumeruPodName)) != nullptr ||
      (pod_name_env = getenv(kTkeStackPodName)) != nullptr) {
    sdk_token_.set_pod_name(pod_name_env);
  }
  char* host_name_env = getenv("HOSTNAME");
  if (host_name_env != nullptr) {
    sdk_token_.set_host_name(host_name_env);
  }

  report_client_interval_ =
      api_config->GetMsOrDefault(constants::kClientReportIntervalKey, constants::kClientReportIntervalDefault);
  if (report_client_interval_ < 10 * 1000) {
    POLARIS_LOG(LOG_ERROR, "api %s must equal or great than 10s", constants::kClientReportIntervalKey);
    return kReturnInvalidConfig;
  }
  cache_clear_time_ =
      api_config->GetMsOrDefault(constants::kApiCacheClearTimeKey, constants::kApiCacheClearTimeDefault);
  if (cache_clear_time_ < 1000) {
    POLARIS_LOG(LOG_ERROR, "api %s must equal or great than 1s", constants::kApiCacheClearTimeKey);
    return kReturnInvalidConfig;
  }

  // 客户端地域信息
  Config* location_config = api_config->GetSubConfig(constants::kApiLocationKey);
  Location location;
  location.region = location_config->GetStringOrDefault(constants::kLocationRegion, "");
  location.zone = location_config->GetStringOrDefault(constants::kLocationZone, "");
  location.campus = location_config->GetStringOrDefault(constants::kLocationCampus, "");
  bool enable_update_location = location_config->GetBoolOrDefault("enableUpdate", true);
  client_location_.Init(location, enable_update_location);
  delete location_config;
  return kReturnOk;
}

ReturnCode ContextImpl::InitGlobalConfig(Config* config, Context* context) {
  // Init server connector plugin
  std::unique_ptr<Config> plugin_config(config->GetSubConfig("serverConnector"));
  Plugin* plugin = nullptr;
  std::string protocol = plugin_config->GetStringOrDefault("protocol", kPluginDefaultServerConnector);
  PluginManager::Instance().GetPlugin(protocol, kPluginServerConnector, plugin);
  server_connector_ = dynamic_cast<ServerConnector*>(plugin);
  if (server_connector_ == nullptr) {
    POLARIS_LOG(LOG_ERROR, "Plugin factory register with name[%s] and type[%s] return error instance", protocol.c_str(),
                PluginTypeToString(kPluginServerConnector));
    return kReturnPluginError;
  }
  ReturnCode ret = server_connector_->Init(plugin_config.get(), context);
  if (ret != kReturnOk) {
    return ret;
  }
  plugin_config.reset();

  // 必须放到server connector之后初始化，保证server connector里的join point已经读取
  if ((ret = InitSystemConfig(config->GetSubConfig("system"))) != kReturnOk) {
    return ret;
  }

  // init api mode and api default timeout
  {
    Config* api_config = config->GetSubConfig("api");
    ret = InitApiConfig(api_config);
    delete api_config;
    if (ret != kReturnOk) {
      return ret;
    }
  }

  // Init stat reporter
  plugin_config.reset(config->GetSubConfig("statReporter"));
  plugin = nullptr;
  std::string plugin_name = plugin_config->GetStringOrDefault("name", kPluginDefaultStatReporter);
  PluginManager::Instance().GetPlugin(plugin_name, kPluginStatReporter, plugin);
  stat_reporter_ = dynamic_cast<StatReporter*>(plugin);
  if (stat_reporter_ == nullptr) {
    POLARIS_LOG(LOG_ERROR, "Plugin factory register with name[%s] and type[%s] return error instance",
                plugin_name.c_str(), PluginTypeToString(kPluginStatReporter));
    return kReturnPluginError;
  }
  ret = stat_reporter_->Init(plugin_config.get(), context);
  if (ret != kReturnOk) {
    return ret;
  }
  plugin_config.reset();

  // Init alert reporter
  plugin_config.reset(config->GetSubConfig("alertReporter"));
  plugin = nullptr;
  plugin_name = plugin_config->GetStringOrDefault("name", kPluginDefaultAlertReporter);
  PluginManager::Instance().GetPlugin(plugin_name, kPluginAlertReporter, plugin);
  alert_reporter_ = dynamic_cast<AlertReporter*>(plugin);
  if (alert_reporter_ == nullptr) {
    POLARIS_LOG(LOG_ERROR, "Plugin factory register with name[%s] and type[%s] return error instance",
                plugin_name.c_str(), PluginTypeToString(kPluginAlertReporter));
    return kReturnPluginError;
  }
  ret = alert_reporter_->Init(plugin_config.get(), context);
  if (ret != kReturnOk) {
    return ret;
  }
  plugin_config.reset();

  // Init server metric
  plugin_config.reset(config->GetSubConfig("serverMetric"));
  plugin = nullptr;
  plugin_name = plugin_config->GetStringOrDefault("name", "");
  if (!plugin_name.empty()) {
    PluginManager::Instance().GetPlugin(plugin_name, kPluginServerMetric, plugin);
    server_metric_.reset(dynamic_cast<ServerMetric*>(plugin));
    if (server_metric_ == nullptr) {
      POLARIS_LOG(LOG_ERROR, "Plugin factory register with name[%s] and type[%s] return error instance",
                  plugin_name.c_str(), PluginTypeToString(kPluginServerMetric));
      return kReturnPluginError;
    }
    ret = server_metric_->Init(plugin_config.get(), context);
  }
  return ret;
}

ReturnCode ContextImpl::InitConsumerConfig(Config* consumer_config, Context* context) {
  // Init local registry
  Config* plugin_config = consumer_config->GetSubConfig("localCache");
  Plugin* plugin = nullptr;
  std::string plugin_name = plugin_config->GetStringOrDefault("type", kPluginDefaultLocalRegistry);
  PluginManager::Instance().GetPlugin(plugin_name, kPluginLocalRegistry, plugin);
  local_registry_ = dynamic_cast<LocalRegistry*>(plugin);
  if (local_registry_ == nullptr) {
    POLARIS_LOG(LOG_ERROR, "Plugin factory register with name[%s] and type[%s] return error instance",
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
  ServiceKey verify_service_key = {"polaris_cpp", "verify_default_config"};
  global_service_config_ = config;
  std::shared_ptr<ServiceContext> verify_context = this->CreateServiceContext(verify_service_key);
  global_service_config_ = nullptr;
  if (verify_context == nullptr) {
    return kReturnInvalidConfig;
  }
  return kReturnOk;
}

ReturnCode ContextImpl::Init(Config* config, Context* context, ContextMode mode) {
  context_mode_ = mode;
  context_ = context;
  ReturnCode ret;
  context_config_.take_effect_time_ = Time::GetSystemTimeMs();
  {
    Config* global_config = config->GetSubConfig("global");
    ReturnCode ret = InitGlobalConfig(global_config, context);
    delete global_config;
    if (ret != kReturnOk) {
      return ret;
    }
  }
  // 必须先创建ServiceRecord对象
  service_record_ = new ServiceRecord();
  api_stat_registry_ = new ApiStatRegistry(context_);
  engine_ = new Engine(context_);

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
  std::vector<Config*> service_config_list = consumer_config->GetSubConfigList("service");
  delete consumer_config;
  if (ret != kReturnOk) {
    return ret;
  }
  for (auto item : service_config_list) {
    ServiceKey service_key = {item->GetStringOrDefault("namespace", ""), item->GetStringOrDefault("name", "")};
    if (service_config_map_.find(service_key) != service_config_map_.end()) {
      POLARIS_LOG(LOG_ERROR, "duplicate service level config for service[%s/%s]", service_key.namespace_.c_str(),
                  service_key.name_.c_str());
      delete item;
      continue;
    }
    if (service_key.name_.empty() || service_key.namespace_.empty()) {
      POLARIS_LOG(LOG_ERROR, "service level config with invalid service[%s/%s]", service_key.namespace_.c_str(),
                  service_key.name_.c_str());
      delete item;
      continue;
    }
    service_config_map_.insert(std::make_pair(service_key, item));
  }
  global_service_config_ = config->GetSubConfigClone("consumer");

  {  // 初始化QuotaManager
    Config* rate_limit_config = config->GetSubConfig("rateLimiter");
    ret = quota_manager_->Init(context_, rate_limit_config);
    delete rate_limit_config;
    if (ret != kReturnOk) {
      return ret;
    }
  }

  context_config_.init_finish_time_ = Time::GetSystemTimeMs();
  context_config_.config_ = config->ToJsonString();
  char current_work_dir[PATH_MAX];
  if (getcwd(current_work_dir, sizeof(current_work_dir)) == nullptr) {
    current_work_dir[0] = '\0';
  }
  POLARIS_LOG(LOG_INFO,
              "===== create context[%s] with config:\n%s\n cwd %s =====", sdk_token_.ShortDebugString().c_str(),
              config->ToString().c_str(), current_work_dir);
  return ret;
}

void ContextImpl::RegisterCache(Clearable* cache) {
  cache->IncrementRef();
  std::lock_guard<std::mutex> lock_guard(cache_lock_);
  last_clear_handler_++;
  cache->SetClearHandler(last_clear_handler_);
  cache_map_.insert(std::make_pair(last_clear_handler_, cache));
}

void ContextImpl::ClearCache() {
  std::vector<uint64_t> delete_cache_;
  std::vector<uint64_t> clear_cache_;
  cache_lock_.lock();
  for (std::map<uint64_t, Clearable*>::iterator it = cache_map_.begin(); it != cache_map_.end(); ++it) {
    if (it->second->GetClearHandler() == it->first) {
      clear_cache_.push_back(it->first);
    } else {
      delete_cache_.push_back(it->first);
    }
  }
  cache_lock_.unlock();
  // 先删除无效cache
  std::map<uint64_t, Clearable*>::iterator it;
  for (std::size_t i = 0; i < delete_cache_.size(); i++) {
    Clearable* clearable = nullptr;
    cache_lock_.lock();
    it = cache_map_.find(delete_cache_[i]);
    if (it != cache_map_.end()) {
      clearable = it->second;
      cache_map_.erase(it);
    }
    cache_lock_.unlock();
    if (clearable != nullptr) {
      clearable->DecrementRef();
    }
  }
  // 清理还在使用的cache
  uint64_t min_access_time = thread_time_mgr_->MinTime() - cache_clear_time_;  // 1s没有访问就清除
  for (std::size_t i = 0; i < clear_cache_.size(); i++) {
    cache_lock_.lock();
    it = cache_map_.find(clear_cache_[i]);
    if (it != cache_map_.end()) {
      it->second->Clear(min_access_time);
    }
    cache_lock_.unlock();
  }
  service_context_map_->CheckGc(min_access_time);
}

}  // namespace polaris
