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

#include "plugin/local_registry/local_registry.h"

#include <inttypes.h>
#include <string.h>

#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "cache/cache_manager.h"
#include "cache/cache_persist.h"
#include "context_internal.h"
#include "logger.h"
#include "model/location.h"
#include "model/model_impl.h"
#include "monitor/service_record.h"
#include "plugin/plugin_manager.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "polaris/log.h"
#include "polaris/plugin.h"

namespace polaris {

ServiceEventHandlerImpl::ServiceEventHandlerImpl(LocalRegistry* local_registry,
                                                 ServiceDataNotify* data_notify) {
  local_registry_ = local_registry;
  data_notify_    = data_notify;
}

ServiceEventHandlerImpl::~ServiceEventHandlerImpl() {
  local_registry_ = NULL;
  if (data_notify_ != NULL) {
    delete data_notify_;
  }
}

void ServiceEventHandlerImpl::OnEventUpdate(const ServiceKey& service_key,
                                            ServiceDataType data_type, void* data) {
  ServiceData* service_data = reinterpret_cast<ServiceData*>(data);
  local_registry_->UpdateServiceData(service_key, data_type, service_data);
  if (service_data != NULL) {
    if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
      POLARIS_LOG(LOG_TRACE, "service event handler fired for: service[%s/%s] with type:%s data:%s",
                  service_key.namespace_.c_str(), service_key.name_.c_str(),
                  DataTypeToStr(data_type), service_data->ToJsonString().c_str());
    }
    data_notify_->Notify(service_data);
  } else {
    delete data_notify_;
    data_notify_ = NULL;
  }
}

void ServiceEventHandlerImpl::OnEventSync(const ServiceKey& service_key,
                                          ServiceDataType data_type) {
  local_registry_->UpdateServiceSyncTime(service_key, data_type);
}

///////////////////////////////////////////////////////////////////////////////

InMemoryRegistry::InMemoryRegistry()
    : context_(NULL), next_service_id_(0), service_expire_time_(0), service_refresh_interval_(0) {
  pthread_rwlock_init(&rwlock_, NULL);
  pthread_rwlock_init(&notify_rwlock_, NULL);
}

InMemoryRegistry::~InMemoryRegistry() {
  context_ = NULL;
  pthread_rwlock_destroy(&rwlock_);
  for (std::map<ServiceKey, Service*>::iterator service_it = service_cache_.begin();
       service_it != service_cache_.end(); ++service_it) {
    delete service_it->second;
  }
  service_cache_.clear();
  pthread_rwlock_destroy(&notify_rwlock_);
  // 只需要清空map即可，notify对象的释放交给ServiceEventHandler处理
  service_data_notify_map_.clear();
}

ReturnCode InMemoryRegistry::Init(Config* config, Context* context) {
  context_             = context;
  service_expire_time_ = config->GetMsOrDefault(LocalRegistryConfig::kServiceExpireTimeKey,
                                                LocalRegistryConfig::kServiceExpireTimeDefault);
  POLARIS_CHECK(service_expire_time_ >= 60 * 1000, kReturnInvalidConfig);

  service_refresh_interval_ =
      config->GetMsOrDefault(LocalRegistryConfig::kServiceRefreshIntervalKey,
                             LocalRegistryConfig::kServiceRefreshIntervalDefault);
  POLARIS_CHECK(service_refresh_interval_ >= 100, kReturnInvalidConfig);

  POLARIS_LOG(LOG_INFO, "service_expire_time:%" PRIu64 " service_refresh_interval:%" PRIu64 "",
              service_expire_time_, service_expire_time_);

  CachePersist& cache_persist = context_->GetContextImpl()->GetCacheManager()->GetCachePersist();
  ReturnCode ret              = cache_persist.Init(config);
  if (ret != kReturnOk) {
    return ret;
  }
  ContextImpl* context_impl = context_->GetContextImpl();
  Location* location        = cache_persist.LoadLocation();
  if (location != NULL) {
    context_impl->GetClientLocation().Update(*location);
    delete location;
  }
  service_interval_map_[context_impl->GetDiscoverService().service_] =
      context_impl->GetDiscoverService().refresh_interval_;
  service_interval_map_[context_impl->GetMonitorService().service_] =
      context_impl->GetMonitorService().refresh_interval_;
  service_interval_map_[context_impl->GetHeartbeatService().service_] =
      context_impl->GetHeartbeatService().refresh_interval_;
  service_interval_map_[context_impl->GetMetricService().service_] =
      context_impl->GetMetricService().refresh_interval_;
  return kReturnOk;
}

void InMemoryRegistry::RunGcTask() {
  // 垃圾回收
  uint64_t min_gc_time = context_->GetContextImpl()->RcuMinTime() - 2000;
  service_instances_data_.CheckGc(min_gc_time);
  service_route_rule_data_.CheckGc(min_gc_time);
  service_rate_limit_data_.CheckGc(min_gc_time);
  service_circuit_breaker_config_data_.CheckGc(min_gc_time);
}

Service* InMemoryRegistry::GetOrCreateServiceInLock(const ServiceKey& service_key) {
  Service* service = NULL;
  pthread_rwlock_wrlock(&rwlock_);
  std::map<ServiceKey, Service*>::iterator service_it = service_cache_.find(service_key);
  if (service_it == service_cache_.end()) {
    service                     = new Service(service_key, ++next_service_id_);
    service_cache_[service_key] = service;
  } else {
    service = service_it->second;
  }
  pthread_rwlock_unlock(&rwlock_);
  return service;
}

void InMemoryRegistry::CheckExpireServiceData(uint64_t min_access_time,
                                              RcuMap<ServiceKey, ServiceData>& rcu_cache,
                                              ServiceDataType service_data_type) {
  std::vector<ServiceKey> expired_services;
  ContextImpl* context_impl = context_->GetContextImpl();
  rcu_cache.CheckExpired(min_access_time, expired_services);
  ServiceKeyWithType service_key_with_type;
  service_key_with_type.data_type_ = service_data_type;
  for (std::size_t i = 0; i < expired_services.size(); ++i) {
    service_key_with_type.service_key_ = expired_services[i];
    pthread_rwlock_wrlock(&notify_rwlock_);
    if (service_data_notify_map_.erase(service_key_with_type) > 0) {  // 有通知对象表示注册过handler
      context_->GetServerConnector()->DeregisterEventHandler(expired_services[i],
                                                             service_data_type);
    } else {  // 没有通知对象，表示未注册过handler，从磁盘加载后从未访问过的数据，直接删除数据
      rcu_cache.Delete(expired_services[i]);
      context_impl->GetServiceRecord()->ServiceDataDelete(expired_services[i], service_data_type);
      context_impl->GetCacheManager()->GetCachePersist().PersistServiceData(expired_services[i],
                                                                            service_data_type, "");
    }
    pthread_rwlock_unlock(&notify_rwlock_);
    if (service_data_type == kServiceDataInstances) {  // 清除实例数据时对应的服务级别插件也删除
      context_impl->DeleteServiceContext(expired_services[i]);
      DeleteServiceInLock(expired_services[i]);
    }
  }
}

void InMemoryRegistry::RemoveExpireServiceData(uint64_t current_time) {
  POLARIS_ASSERT(current_time > service_expire_time_);
  uint64_t min_access_time = current_time - service_expire_time_;
  CheckExpireServiceData(min_access_time, service_instances_data_, kServiceDataInstances);
  CheckExpireServiceData(min_access_time, service_route_rule_data_, kServiceDataRouteRule);
  CheckExpireServiceData(min_access_time, service_rate_limit_data_, kServiceDataRateLimit);
  CheckExpireServiceData(min_access_time, service_circuit_breaker_config_data_,
                         kCircuitBreakerConfig);
}

ReturnCode InMemoryRegistry::GetServiceDataWithRef(const ServiceKey& service_key,
                                                   ServiceDataType data_type,
                                                   ServiceData*& service_data) {
  if (data_type == kServiceDataInstances) {
    service_data = service_instances_data_.Get(service_key);
  } else if (data_type == kServiceDataRouteRule) {
    service_data = service_route_rule_data_.Get(service_key);
  } else if (data_type == kServiceDataRateLimit) {
    service_data = service_rate_limit_data_.Get(service_key);
  } else if (data_type == kCircuitBreakerConfig) {
    service_data = service_circuit_breaker_config_data_.Get(service_key);
  }
  if (service_data != NULL) {
    if (service_data->GetDataStatus() < kDataIsSyncing) {
      return kReturnNotInit;
    } else {
      return kReturnOk;
    }
  } else {
    return kReturnServiceNotFound;
  }
}

ServiceDataNotify* InMemoryRegistry::GetOrCreateDataNotify(const ServiceKey& service_key,
                                                           ServiceDataType data_type,
                                                           bool& new_create) {
  new_create                     = false;
  ServiceDataNotify* data_notify = NULL;
  ServiceKeyWithType service_key_with_type;
  service_key_with_type.service_key_ = service_key;
  service_key_with_type.data_type_   = data_type;
  std::map<ServiceKeyWithType, ServiceDataNotify*>::iterator notify_it =
      service_data_notify_map_.find(service_key_with_type);
  if (notify_it != service_data_notify_map_.end()) {
    data_notify = notify_it->second;
  } else {
    data_notify                                     = new ServiceDataNotify(service_key, data_type);
    service_data_notify_map_[service_key_with_type] = data_notify;
    new_create                                      = true;
  }
  POLARIS_ASSERT(data_notify != NULL);
  return data_notify;
}

ReturnCode InMemoryRegistry::LoadServiceDataWithNotify(const ServiceKey& service_key,
                                                       ServiceDataType data_type,
                                                       ServiceData*& service_data,
                                                       ServiceDataNotify*& data_notify) {
  bool new_create = false;
  pthread_rwlock_wrlock(&notify_rwlock_);  // TODO 使用读写锁分离查询和创建操作
  data_notify = this->GetOrCreateDataNotify(service_key, data_type, new_create);
  if (new_create) {  // 只有首次创建注册更新任务
    ServerConnector* server_connector = context_->GetServerConnector();
    ServiceEventHandler* handler      = new ServiceEventHandlerImpl(this, data_notify);
    uint64_t refresh_interval         = service_refresh_interval_;
    std::map<ServiceKey, uint64_t>::iterator interval_it = service_interval_map_.find(service_key);
    if (interval_it != service_interval_map_.end()) {
      refresh_interval = interval_it->second;
    }
    // 先加载磁盘缓存数据
    CachePersist& cache_persist = context_->GetContextImpl()->GetCacheManager()->GetCachePersist();
    ServiceData* disk_service_data = cache_persist.LoadServiceData(service_key, data_type);
    if (disk_service_data != NULL) {
      this->UpdateServiceData(service_key, data_type, disk_service_data);
      if (service_data == NULL) {
        disk_service_data->IncrementRef();
        service_data = disk_service_data;
      }
    }
    server_connector->RegisterEventHandler(service_key, data_type, refresh_interval, handler);
  }
  pthread_rwlock_unlock(&notify_rwlock_);
  if (new_create) {
    POLARIS_LOG(LOG_INFO, "load %s data with notify for service[%s/%s]", DataTypeToStr(data_type),
                service_key.namespace_.c_str(), service_key.name_.c_str());
  }
  return kReturnOk;
}

void InMemoryRegistry::DeleteServiceInLock(const ServiceKey& service_key) {
  pthread_rwlock_wrlock(&rwlock_);
  std::map<ServiceKey, Service*>::iterator service_it = service_cache_.find(service_key);
  if (service_it != service_cache_.end()) {
    delete service_it->second;
    service_cache_.erase(service_it);
  }
  pthread_rwlock_unlock(&rwlock_);
}

ReturnCode InMemoryRegistry::UpdateServiceData(const ServiceKey& service_key,
                                               ServiceDataType data_type,
                                               ServiceData* service_data) {
  if (service_data != NULL) {  // 更新服务数据指向服务
    Service* service = GetOrCreateServiceInLock(service_key);
    service->UpdateData(service_data);
  }
  ContextImpl* context_impl = context_->GetContextImpl();
  if (data_type == kServiceDataInstances) {
    ServiceData* old_service_data = service_instances_data_.Get(service_key);
    if (old_service_data != NULL) {
      PluginManager::Instance().OnPreUpdateServiceData(old_service_data, service_data);
      old_service_data->DecrementRef();
    }
    service_instances_data_.Update(service_key, service_data);
  } else if (data_type == kServiceDataRouteRule) {
    if (service_data != NULL) {  // 填充环境变量
      const SystemVariables& system_variables = context_impl->GetSystemVariables();
      service_data->GetServiceDataImpl()->FillSystemVariables(system_variables);
    }
    service_route_rule_data_.Update(service_key, service_data);
  } else if (data_type == kServiceDataRateLimit) {
    service_rate_limit_data_.Update(service_key, service_data);
  } else if (data_type == kCircuitBreakerConfig) {
    service_circuit_breaker_config_data_.Update(service_key, service_data);
  } else {
    POLARIS_ASSERT(false);
  }
  if (service_data == NULL) {  // Server Connector反注册Handler触发更新为NULL
    if (data_type == kServiceDataInstances) {  // 删除服务实例数据时，同时删除服务
      context_impl->DeleteServiceContext(service_key);
      DeleteServiceInLock(service_key);
    }
    context_impl->GetServiceRecord()->ServiceDataDelete(service_key,
                                                        data_type);  // 同步记录Service数据删除
    context_impl->GetCacheManager()->GetCachePersist().PersistServiceData(
        service_key, data_type, "");  // 异步删除磁盘服务数据
    return kReturnOk;
  }
  context_impl->GetServiceRecord()->ServiceDataUpdate(service_data);  // 同步记录Service版本变化
  if (service_data->GetDataStatus() == kDataInitFromDisk) {
    return kReturnOk;  // 磁盘数据无需回写磁盘缓存
  }
  context_impl->GetCacheManager()->SubmitServiceDataChange(service_data);
  if (service_data->GetDataStatus() ==
      kDataNotFound) {  // 服务不存在的数据不存入本地缓存，则尝试删除之前的缓存
    context_impl->GetCacheManager()->GetCachePersist().PersistServiceData(service_key, data_type,
                                                                          "");
  } else {
    context_impl->GetCacheManager()->GetCachePersist().PersistServiceData(
        service_data->GetServiceKey(), service_data->GetDataType(), service_data->ToJsonString());
  }
  return kReturnOk;
}

ReturnCode InMemoryRegistry::UpdateServiceSyncTime(const ServiceKey& service_key,
                                                   ServiceDataType data_type) {
  ContextImpl* context_impl = context_->GetContextImpl();
  context_impl->GetCacheManager()->GetCachePersist().UpdateSyncTime(service_key, data_type);
  return kReturnOk;
}

ReturnCode InMemoryRegistry::UpdateCircuitBreakerData(
    const ServiceKey& service_key, const CircuitBreakerData& circuit_breaker_data) {
  Service* service;
  pthread_rwlock_rdlock(&rwlock_);
  std::map<ServiceKey, Service*>::iterator service_it = service_cache_.find(service_key);
  if (service_it == service_cache_.end()) {
    pthread_rwlock_unlock(&rwlock_);
    POLARIS_LOG(LOG_WARN, "Update circuit breaker status failed because service[%s/%s] not found",
                service_key.namespace_.c_str(), service_key.name_.c_str());
    return kReturnOk;
  }
  service = service_it->second;
  pthread_rwlock_unlock(&rwlock_);
  service->SetCircuitBreakerData(circuit_breaker_data);
  return kReturnOk;
}

ReturnCode InMemoryRegistry::UpdateDynamicWeight(const ServiceKey& /*service_key*/,
                                                 const DynamicWeightData& /*dynamic_weight_data*/) {
  return kReturnOk;
}

ReturnCode InMemoryRegistry::GetAllServiceKey(std::set<ServiceKey>& service_key_set) {
  pthread_rwlock_wrlock(&rwlock_);
  std::map<ServiceKey, Service*>::iterator iter = service_cache_.begin();
  for (; iter != service_cache_.end(); ++iter) {
    const ServiceKey& service_key = iter->first;
    service_key_set.insert(service_key);
  }
  pthread_rwlock_unlock(&rwlock_);
  return kReturnOk;
}

ReturnCode InMemoryRegistry::UpdateSetCircuitBreakerData(
    const ServiceKey& service_key, const CircuitBreakUnhealthySetsData& unhealthy_sets) {
  Service* service;
  pthread_rwlock_rdlock(&rwlock_);
  std::map<ServiceKey, Service*>::iterator service_it = service_cache_.find(service_key);
  if (service_it == service_cache_.end()) {
    pthread_rwlock_unlock(&rwlock_);
    POLARIS_LOG(LOG_WARN,
                "Update set circuit breaker status failed because service[%s/%s] not found",
                service_key.namespace_.c_str(), service_key.name_.c_str());
    return kReturnOk;
  }
  service = service_it->second;
  pthread_rwlock_unlock(&rwlock_);
  return service->WriteCircuitBreakerUnhealthySets(unhealthy_sets);
}

ReturnCode InMemoryRegistry::GetCircuitBreakerInstances(const ServiceKey& service_key,
                                                        ServiceData*& service_data,
                                                        std::vector<Instance*>& open_instances) {
  service_data = service_instances_data_.Get(service_key, false);
  if (service_data == NULL) {
    return kReturnServiceNotFound;
  }
  if (service_data->GetDataStatus() < kDataIsSyncing) {
    service_data->DecrementRef();
    return kReturnServiceNotFound;
  }
  Service* service = service_data->GetService();
  ServiceInstances service_instances(service_data);
  std::map<std::string, Instance*>& instance_map      = service_instances.GetInstances();
  std::set<std::string> circuit_breaker_open_instance = service->GetCircuitBreakerOpenInstances();
  for (std::set<std::string>::iterator it = circuit_breaker_open_instance.begin();
       it != circuit_breaker_open_instance.end(); ++it) {
    const std::string& instance_id                  = *it;
    std::map<std::string, Instance*>::iterator iter = instance_map.find(instance_id);
    if (iter == instance_map.end()) {
      POLARIS_LOG(LOG_INFO, "The outlier detector of service[%s/%s] getting instance[%s] failed",
                  service_key.namespace_.c_str(), service_key.name_.c_str(), instance_id.c_str());
      continue;
    }
    open_instances.push_back(iter->second);
  }
  if (open_instances.empty()) {
    return kReturnInstanceNotFound;
  }
  service_data->IncrementRef();
  return kReturnOk;
}

}  // namespace polaris
