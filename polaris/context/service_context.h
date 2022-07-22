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

#ifndef POLARIS_CPP_POLARIS_CONTEXT_SERVICE_CONTEXT_H_
#define POLARIS_CPP_POLARIS_CONTEXT_SERVICE_CONTEXT_H_

#include <functional>
#include <memory>

#include "cache/rcu_unordered_map.h"
#include "cache/service_cache.h"
#include "plugin/circuit_breaker/chain.h"
#include "plugin/health_checker/health_checker.h"
#include "plugin/service_router/router_chain.h"

namespace std {
template <>
struct hash<polaris::ServiceKey> {
  std::size_t operator()(const polaris::ServiceKey& service_key) const {
    return hash<string>()(service_key.name_) + service_key.namespace_.size();
  }
};

}  // namespace std

namespace polaris {

class WeightAdjuster;

/// @brief 服务级别上下文
class ServiceContext {
 public:
  ServiceContext();

  ~ServiceContext();

  ReturnCode Init(const ServiceKey& service_key, Config* config, Config* global_config, Context* context);

  ServiceRouterChain* GetServiceRouterChain() const { return service_router_chain_; }

  LoadBalancer* GetLoadBalancer(const LoadBalanceType& load_balance_type);

  WeightAdjuster* GetWeightAdjuster() const { return weight_adjuster_; }

  CircuitBreakerChain* GetCircuitBreakerChain() const { return circuit_breaker_chain_; }

  HealthCheckerChain* GetHealthCheckerChain() const { return health_checker_chain_; }

  ReturnCode DoRoute(RouteInfo& route_info, RouteResult* route_result);

  ServiceData* GetInstances();

  ServiceData* GetRoutings();

  void UpdateInstances(ServiceData* instances);

  void UpdateRoutings(ServiceData* routings);

  bool CheckInstanceExist(const std::string& instance_id);

  uint64_t GetCircuitBreakerVersion() const { return circuit_breaker_version_.load(std::memory_order_relaxed); }

  // 更新服务熔断数据版本号，并执行缓存构建
  void UpdateCircuitBreaker(const ServiceKey& service_key, uint64_t circuit_breaker_version);

  void BuildCacheForDynamicWeight(const ServiceKey& service_key, uint64_t dynamic_weight_version);

 private:
  // 注册需要触发更新缓存的请求
  void AddCacheUpdate(RouteInfo& route_info);

  std::set<ServiceCacheUpdateParam> GetAllCacheUpdate();

  // 重建服务路由和负载均衡缓存
  bool UpdateCache(RouteInfo& route_info, const ServiceCacheUpdateParam& update_param,
                   uint64_t dynamic_weight_version = 0);

 private:
  Context* context_;
  ServiceRouterChain* service_router_chain_;
  LoadBalanceType config_lb_type_;
  std::shared_ptr<LoadBalancer> load_balancer_;
  RcuUnorderedMap<LoadBalanceType, LoadBalancer> lb_map_;
  WeightAdjuster* weight_adjuster_;
  CircuitBreakerChain* circuit_breaker_chain_;
  HealthCheckerChain* health_checker_chain_;
  std::atomic<ServiceData*> service_instance_;
  std::atomic<ServiceData*> service_routings_;
  std::atomic<uint64_t> circuit_breaker_version_;

  std::mutex cache_lock_;
  std::set<ServiceCacheUpdateParam> cache_update_set_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CONTEXT_SERVICE_CONTEXT_H_
