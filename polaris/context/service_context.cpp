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

#include "context/service_context.h"

#include "context/context_impl.h"
#include "plugin/plugin_manager.h"
#include "plugin/weight_adjuster/weight_adjuster.h"

namespace polaris {

ServiceContext::ServiceContext()
    : context_(nullptr),
      service_router_chain_(nullptr),
      config_lb_type_(kLoadBalanceTypeDefaultConfig),
      load_balancer_(nullptr),
      weight_adjuster_(nullptr),
      circuit_breaker_chain_(nullptr),
      health_checker_chain_(nullptr),
      service_instance_(nullptr),
      service_routings_(nullptr),
      circuit_breaker_version_(0) {}

ServiceContext::~ServiceContext() {
  context_ = nullptr;
  if (service_router_chain_ != nullptr) {
    delete service_router_chain_;
    service_router_chain_ = nullptr;
  }
  if (weight_adjuster_ != nullptr) {
    delete weight_adjuster_;
    weight_adjuster_ = nullptr;
  }
  if (circuit_breaker_chain_ != nullptr) {
    delete circuit_breaker_chain_;
    circuit_breaker_chain_ = nullptr;
  }
  if (health_checker_chain_ != nullptr) {
    delete health_checker_chain_;
    health_checker_chain_ = nullptr;
  }
  if (service_instance_ != nullptr) {
    service_instance_.load()->DecrementRef();
    service_instance_ = nullptr;
  }
  if (service_routings_ != nullptr) {
    service_routings_.load()->DecrementRef();
    service_routings_ = nullptr;
  }
}

ServiceData* ServiceContext::GetInstances() { return service_instance_.load(std::memory_order_relaxed); }

ServiceData* ServiceContext::GetRoutings() { return service_routings_.load(std::memory_order_relaxed); }

void ServiceContext::UpdateInstances(ServiceData* instances) {
  // 比较新旧数据，用于动态权重
  weight_adjuster_->ServiceInstanceUpdate(instances, service_instance_.load(std::memory_order_relaxed));

  if (instances != nullptr) {
    instances->IncrementRef();
    if (service_instance_.load(std::memory_order_acquire) != nullptr) {
      std::set<ServiceCacheUpdateParam> cache_update_set = GetAllCacheUpdate();
      for (auto item : cache_update_set) {
        RouteInfo route_info(instances->GetServiceKey(), item.GetSourceServiceInfo());
        route_info.SetServiceInstances(new ServiceInstances(instances));
        route_info.SetCircuitBreakerVersion(circuit_breaker_version_);
        UpdateCache(route_info, item);
      }
    }
  }
  ServiceData* old_instances = service_instance_.exchange(instances, std::memory_order_release);
  if (old_instances != nullptr) {
    old_instances->DecrementRef();
  }
}

bool ServiceContext::CheckInstanceExist(const std::string& instance_id) {
  auto service_data = GetInstances();
  if (service_data == nullptr) {
    return false;
  }
  ServiceInstances service_instances(service_data);
  return service_instances.GetInstances().count(instance_id) > 0;
}

void ServiceContext::UpdateRoutings(ServiceData* routings) {
  if (routings != nullptr) {
    routings->IncrementRef();
    if (service_routings_.load(std::memory_order_acquire) != nullptr) {
      std::set<ServiceCacheUpdateParam> cache_update_set = GetAllCacheUpdate();
      for (auto item : cache_update_set) {
        RouteInfo route_info(routings->GetServiceKey(), item.GetSourceServiceInfo());
        route_info.SetServiceRouteRule(new ServiceRouteRule(routings));
        route_info.SetCircuitBreakerVersion(circuit_breaker_version_);
        UpdateCache(route_info, item);
      }
    }
  }
  ServiceData* old_routings = service_routings_.exchange(routings, std::memory_order_release);
  if (old_routings != nullptr) {
    old_routings->DecrementRef();
  }
}

static Config* ServiceOrGlobalConfig(Config* service_config, Config* global_config, const std::string& key) {
  return service_config->SubConfigExist(key) ? service_config->GetSubConfig(key) : global_config->GetSubConfig(key);
}

ReturnCode ServiceContext::Init(const ServiceKey& service_key, Config* config, Config* global_config,
                                Context* context) {
  context_ = context;
  // 初始化路由插件
  Config* plugin_config = ServiceOrGlobalConfig(config, global_config, "serviceRouter");
  service_router_chain_ = new ServiceRouterChain(service_key);
  ReturnCode ret = service_router_chain_->Init(plugin_config, context);
  delete plugin_config;
  if (ret != kReturnOk) {
    return ret;
  }

  // 初始化负载均衡插件
  plugin_config = ServiceOrGlobalConfig(config, global_config, "loadBalancer");
  Plugin* plugin = nullptr;
  std::string plugin_name = plugin_config->GetStringOrDefault("type", kLoadBalanceTypeWeightedRandom);
  PluginManager::Instance().GetPlugin(plugin_name, kPluginLoadBalancer, plugin);
  load_balancer_.reset(dynamic_cast<LoadBalancer*>(plugin));
  if (load_balancer_ == nullptr) {
    POLARIS_LOG(LOG_ERROR, "Plugin factory register with name[%s] and type[%s] return error load balancer instance",
                plugin_name.c_str(), PluginTypeToString(kPluginLoadBalancer));
    delete plugin;
    delete plugin_config;
    return kReturnPluginError;
  }
  ret = load_balancer_->Init(plugin_config, context);
  delete plugin_config;
  config_lb_type_ = load_balancer_->GetLoadBalanceType();
  lb_map_.Update(config_lb_type_, load_balancer_);
  if (ret != kReturnOk) {
    return ret;
  }

  // 初始化探活插件
  constexpr char kOutlierDetectionKey[] = "outlierDetection";
  constexpr char kHealthCheckKey[] = "healthCheck";
  plugin_config = nullptr;
  if (config->SubConfigExist(kOutlierDetectionKey)) {
    plugin_config = config->GetSubConfig(kOutlierDetectionKey);
  } else if (config->SubConfigExist(kHealthCheckKey)) {
    plugin_config = config->GetSubConfig(kHealthCheckKey);
  } else if (global_config->SubConfigExist(kOutlierDetectionKey)) {
    plugin_config = global_config->GetSubConfig(kOutlierDetectionKey);
  } else {
    plugin_config = global_config->GetSubConfig(kHealthCheckKey);
  }
  health_checker_chain_ = new HealthCheckerChainImpl(service_key, context->GetLocalRegistry());
  ret = health_checker_chain_->Init(plugin_config, context);
  delete plugin_config;
  if (ret != kReturnOk) {
    return ret;
  }

  // 初始化熔断插件
  plugin_config = ServiceOrGlobalConfig(config, global_config, "circuitBreaker");
  circuit_breaker_chain_ = new CircuitBreakerChain(service_key);
  ret = circuit_breaker_chain_->Init(plugin_config, context, health_checker_chain_->GetWhen());
  delete plugin_config;
  if (ret != kReturnOk) {
    return ret;
  }

  // 初始化动态权重调整插件
  plugin_config = ServiceOrGlobalConfig(config, global_config, "weightAdjuster");
  plugin = nullptr;
  plugin_name = plugin_config->GetStringOrDefault("name", kPluginDefaultWeightAdjuster);
  PluginManager::Instance().GetPlugin(plugin_name, kPluginWeightAdjuster, plugin);
  weight_adjuster_ = dynamic_cast<WeightAdjuster*>(plugin);
  if (weight_adjuster_ == nullptr) {
    POLARIS_LOG(LOG_ERROR, "Plugin factory register with name[%s] and type[%s] return  error weight adjuster instance",
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

ReturnCode ServiceContext::DoRoute(RouteInfo& route_info, RouteResult* route_result) {
  ReturnCode ret_code = service_router_chain_->DoRoute(route_info, route_result);
  if (route_result->NewInstancesSet()) {
    AddCacheUpdate(route_info);  // 有更新更新，且是内部触发缓存更新时记录请求
  }
  return ret_code;
}

LoadBalancer* ServiceContext::GetLoadBalancer(const LoadBalanceType& load_balance_type) {
  if (load_balance_type == kLoadBalanceTypeDefaultConfig || load_balance_type == config_lb_type_) {
    return load_balancer_.get();
  }

  LoadBalancer* load_balancer = lb_map_.GetWithRcuTime(load_balance_type);
  if (load_balancer != nullptr) {
    return load_balancer;
  }

  std::shared_ptr<LoadBalancer> created_load_balancer = lb_map_.CreateOrGet(load_balance_type, [=] {
    std::unique_ptr<Config> config(Config::CreateEmptyConfig());
    Plugin* plugin = nullptr;
    PluginManager::Instance().GetPlugin(load_balance_type, kPluginLoadBalancer, plugin);
    std::shared_ptr<LoadBalancer> new_load_balancer(dynamic_cast<LoadBalancer*>(plugin));
    if (new_load_balancer == nullptr) {
      POLARIS_LOG(LOG_ERROR, "failed to get load balance plugin : %s", load_balance_type.c_str());
      return new_load_balancer;
    }
    ReturnCode ret_code = new_load_balancer->Init(config.get(), context_);
    if (ret_code != kReturnOk) {
      POLARIS_LOG(LOG_ERROR, "failed to init load balancer : %s", load_balance_type.c_str());
      new_load_balancer.reset();
    }
    return new_load_balancer;
  });
  return created_load_balancer.get();
}

void ServiceContext::AddCacheUpdate(RouteInfo& route_info) {
  ServiceCacheUpdateParam update_param;
  if (route_info.GetSourceServiceInfo() != nullptr) {
    update_param.source_service_info_ = *route_info.GetSourceServiceInfo();
  }
  update_param.request_flag_ = route_info.GetRequestFlags();
  update_param.metadata_param_.failover_type_ = route_info.GetMetadataFailoverType();
  update_param.metadata_param_.metadata_ = route_info.GetMetadata();

  std::lock_guard<std::mutex> lock_guard(cache_lock_);
  cache_update_set_.insert(update_param);
}

std::set<ServiceCacheUpdateParam> ServiceContext::GetAllCacheUpdate() {
  std::lock_guard<std::mutex> lock_guard(cache_lock_);
  return cache_update_set_;
}

void ServiceContext::UpdateCircuitBreaker(const ServiceKey& service_key, uint64_t circuit_breaker_version) {
  std::set<ServiceCacheUpdateParam> cache_update_set = GetAllCacheUpdate();
  for (auto item : cache_update_set) {
    RouteInfo route_info(service_key, item.GetSourceServiceInfo());
    route_info.SetCircuitBreakerVersion(circuit_breaker_version);
    UpdateCache(route_info, item);
  }
  circuit_breaker_version_ = circuit_breaker_version;
}

void ServiceContext::BuildCacheForDynamicWeight(const ServiceKey& service_key, uint64_t dynamic_weight_version) {
  std::set<ServiceCacheUpdateParam> cache_update_set = GetAllCacheUpdate();
  for (auto item : cache_update_set) {
    RouteInfo route_info(service_key, item.GetSourceServiceInfo());
    route_info.SetCircuitBreakerVersion(circuit_breaker_version_);
    UpdateCache(route_info, item, dynamic_weight_version);
  }
}

bool ServiceContext::UpdateCache(RouteInfo& route_info, const ServiceCacheUpdateParam& update_param,
                                 uint64_t dynamic_weight_version) {
  const ServiceKey& service_key = route_info.GetServiceKey();
  POLARIS_LOG(LOG_DEBUG, "refresh cache for service[%s/%s]", service_key.namespace_.c_str(), service_key.name_.c_str());

  route_info.SetRequestFlags(update_param.request_flag_);
  route_info.SetMetadataPara(update_param.metadata_param_);
  ReturnCode ret_code = service_router_chain_->PrepareRouteInfo(route_info, 0);
  if (ret_code != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "prepare route info for service[%s/%s] with error:%s", service_key.namespace_.c_str(),
                service_key.name_.c_str(), ReturnCodeToMsg(ret_code).c_str());
    return false;
  }
  RouteResult route_result;
  ret_code = service_router_chain_->DoRoute(route_info, &route_result);
  if (POLARIS_UNLIKELY(ret_code != kReturnOk)) {
    POLARIS_LOG(LOG_ERROR, "get one instance for service[%s/%s] with route chain retrun error:%s",
                service_key.namespace_.c_str(), service_key.name_.c_str(), ReturnCodeToMsg(ret_code).c_str());
    return ret_code;
  }

  // 获取过滤结果
  ServiceInstances* service_instances = route_info.GetServiceInstances();
  if (dynamic_weight_version > 0) {
    service_instances->SetTempDynamicWeightVersion(dynamic_weight_version);
  }
  std::vector<std::shared_ptr<LoadBalancer>> load_balancers;
  lb_map_.GetAllValues(load_balancers);
  Criteria criteria;
  for (auto lb : load_balancers) {
    Instance* instance = nullptr;
    criteria.ignore_half_open_ = true;  // 避免异步构建缓存时分配半开节点
    lb->ChooseInstance(service_instances, criteria, instance);
    if (instance != nullptr && instance->GetLocalityAwareInfo() > 0) {
      delete instance;
    }
  }
  return true;
}

}  // namespace polaris
