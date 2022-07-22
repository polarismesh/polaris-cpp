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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_LOCAL_REGISTRY_LOCAL_REGISTRY_H_
#define POLARIS_CPP_POLARIS_PLUGIN_LOCAL_REGISTRY_LOCAL_REGISTRY_H_

#include <pthread.h>
#include <stdint.h>

#include <map>
#include <set>

#include "cache/rcu_map.h"
#include "model/model_impl.h"
#include "plugin/server_connector/server_connector.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "polaris/plugin.h"

namespace polaris {

class Config;
class Context;

namespace LocalRegistryConfig {
static const char kServiceExpireTimeKey[] = "serviceExpireTime";
static const uint64_t kServiceExpireTimeDefault = 24 * 60 * 60 * 1000;  // 24 hours

static const char kServiceRefreshIntervalKey[] = "serviceRefreshInterval";
static const uint64_t kServiceRefreshIntervalDefault = 2000;  // 2s
}  // namespace LocalRegistryConfig

class ServiceEventHandlerImpl : public ServiceEventHandler {
 public:
  ServiceEventHandlerImpl(LocalRegistry* local_registry, ServiceDataNotify* data_notify);

  virtual ~ServiceEventHandlerImpl();

  virtual void OnEventUpdate(const ServiceKey& service_key, ServiceDataType data_type, void* data);

  virtual void OnEventSync(const ServiceKey& service_key, ServiceDataType data_type);

 private:
  LocalRegistry* local_registry_;
  ServiceDataNotify* data_notify_;
};

class InMemoryRegistry : public LocalRegistry {
 public:
  InMemoryRegistry();

  virtual ~InMemoryRegistry();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual void RunGcTask();

  virtual void RemoveExpireServiceData();

  virtual ReturnCode GetServiceDataWithRef(const ServiceKey& service_key, ServiceDataType data_type,
                                           ServiceData*& service_data);

  // 触发加载服务数据并返回服务加载通知对象
  virtual ReturnCode LoadServiceDataWithNotify(const ServiceKey& service_key, ServiceDataType data_type,
                                               ServiceData*& service_data, ServiceDataNotify*& notify);

  // 更新缓存中的服务数据
  /*
   * @desc 更新缓冲中的数据
   * @param service_key 需要更新的服务(namespace, servicename)
   * @param data_type
   * @param service_data 新的值。如果是 nullptr 则表示删除
   * @return ReturnCode 具体参考枚举常量
   */
  virtual ReturnCode UpdateServiceData(const ServiceKey& service_key, ServiceDataType data_type,
                                       ServiceData* service_data);

  virtual ReturnCode UpdateServiceSyncTime(const ServiceKey& service_key, ServiceDataType data_type);

  virtual ReturnCode UpdateCircuitBreakerData(const ServiceKey& service_key,
                                              const CircuitBreakerData& circuit_breaker_data);

  virtual ReturnCode UpdateSetCircuitBreakerData(const ServiceKey& service_key,
                                                 const CircuitBreakUnhealthySetsData& unhealthy_sets);

  virtual ReturnCode GetCircuitBreakerInstances(const ServiceKey& service_key, ServiceData*& service_data,
                                                std::vector<Instance*>& open_instances);

  virtual ReturnCode UpdateDynamicWeight(const ServiceKey& service_key, const DynamicWeightData& dynamic_weight_data);

  virtual ReturnCode UpdateInstanceDynamicWeight(ServiceData* instances_service_data,
                                                 const std::map<std::string, uint32_t>& dynamic_weights);

  virtual ReturnCode GetAllServiceKey(std::set<ServiceKey>& service_key_set);

  virtual void CheckAndSetExpireDynamicWeightServiceData(const ServiceKey& service_key);

 private:
  // 创建并获取服务通知对象，必须加锁访问
  ServiceDataNotify* GetOrCreateDataNotify(const ServiceKey& service_key, ServiceDataType data_type, bool& new_create);

  Service* CreateServiceInLock(const ServiceKey& service_key);

  Service* GetServiceInLock(const ServiceKey& service_key);
  std::map<std::string, uint32_t> GetDynamicWeightDataWithLock(const ServiceKey& service_key);

  void DeleteServiceInLock(const ServiceKey& service_key);

  void CheckExpireService(uint64_t min_access_time);

  void CheckExpireServiceData(uint64_t min_access_time, RcuMap<ServiceKey, ServiceData>& rcu_cache,
                              ServiceDataType service_data_type);

 private:
  Context* context_;
  std::map<ServiceKey, uint64_t> service_interval_map_;

  // 1. 服务数据通知对象：用于通知服务数据的加载
  pthread_rwlock_t notify_rwlock_;
  std::map<ServiceKeyWithType, ServiceDataNotify*> service_data_notify_map_;

  // 2. 服务：用于组织服务的熔断状态数据
  pthread_rwlock_t rwlock_;
  uint32_t next_service_id_;  // 用于生成本地服务ID
  std::map<ServiceKey, Service*> service_cache_;

  // 3. 服务数据：用于存储从服务器加载的服务数据
  RcuMap<ServiceKey, ServiceData> service_instances_data_;
  RcuMap<ServiceKey, ServiceData> service_route_rule_data_;
  RcuMap<ServiceKey, ServiceData> service_rate_limit_data_;
  RcuMap<ServiceKey, ServiceData> service_circuit_breaker_config_data_;

  uint64_t service_expire_time_;
  uint64_t service_refresh_interval_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_LOCAL_REGISTRY_LOCAL_REGISTRY_H_
