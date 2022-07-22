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
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cache/cache_manager.h"
#include "cache/cache_persist.h"
#include "context/context_impl.h"
#include "context/service_context.h"
#include "logger.h"
#include "model/instance.h"
#include "model/location.h"
#include "model/model_impl.h"
#include "monitor/service_record.h"
#include "plugin/plugin_manager.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "polaris/log.h"

namespace polaris {

ServiceEventHandlerImpl::ServiceEventHandlerImpl(LocalRegistry* local_registry, ServiceDataNotify* data_notify) {
  local_registry_ = local_registry;
  data_notify_ = data_notify;
}

ServiceEventHandlerImpl::~ServiceEventHandlerImpl() {
  local_registry_ = nullptr;
  if (data_notify_ != nullptr) {
    delete data_notify_;
  }
}

void ServiceEventHandlerImpl::OnEventUpdate(const ServiceKey& service_key, ServiceDataType data_type, void* data) {
  ServiceData* service_data = reinterpret_cast<ServiceData*>(data);
  local_registry_->UpdateServiceData(service_key, data_type, service_data);
  if (service_data != nullptr) {
    if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
      POLARIS_LOG(LOG_TRACE, "service event handler fired for: service[%s/%s] with type:%s data:%s",
                  service_key.namespace_.c_str(), service_key.name_.c_str(), DataTypeToStr(data_type),
                  service_data->ToJsonString().c_str());
    }
    data_notify_->Notify(service_data);
  } else {
    delete data_notify_;
    data_notify_ = nullptr;
  }
}

void ServiceEventHandlerImpl::OnEventSync(const ServiceKey& service_key, ServiceDataType data_type) {
  local_registry_->UpdateServiceSyncTime(service_key, data_type);
}

///////////////////////////////////////////////////////////////////////////////

InMemoryRegistry::InMemoryRegistry()
    : context_(nullptr), next_service_id_(0), service_expire_time_(0), service_refresh_interval_(0) {
  pthread_rwlock_init(&rwlock_, nullptr);
  pthread_rwlock_init(&notify_rwlock_, nullptr);
}

InMemoryRegistry::~InMemoryRegistry() {
  context_ = nullptr;
  pthread_rwlock_destroy(&rwlock_);
  for (std::map<ServiceKey, Service*>::iterator service_it = service_cache_.begin(); service_it != service_cache_.end();
       ++service_it) {
    delete service_it->second;
  }
  service_cache_.clear();
  pthread_rwlock_destroy(&notify_rwlock_);
  // 只需要清空map即可，notify对象的释放交给ServiceEventHandler处理
  service_data_notify_map_.clear();
}

ReturnCode InMemoryRegistry::Init(Config* config, Context* context) {
  context_ = context;
  service_expire_time_ = config->GetMsOrDefault(LocalRegistryConfig::kServiceExpireTimeKey,
                                                LocalRegistryConfig::kServiceExpireTimeDefault);
  POLARIS_CHECK(service_expire_time_ >= 60 * 1000, kReturnInvalidConfig);

  service_refresh_interval_ = config->GetMsOrDefault(LocalRegistryConfig::kServiceRefreshIntervalKey,
                                                     LocalRegistryConfig::kServiceRefreshIntervalDefault);
  POLARIS_CHECK(service_refresh_interval_ >= 100, kReturnInvalidConfig);

  POLARIS_LOG(LOG_INFO, "service_expire_time:%" PRIu64 " service_refresh_interval:%" PRIu64 "", service_expire_time_,
              service_expire_time_);

  CachePersist& cache_persist = context_->GetContextImpl()->GetCacheManager()->GetCachePersist();
  ReturnCode ret = cache_persist.Init(config);
  if (ret != kReturnOk) {
    return ret;
  }
  ContextImpl* context_impl = context_->GetContextImpl();
  std::unique_ptr<Location> location = cache_persist.LoadLocation();
  if (location != nullptr) {
    context_impl->GetClientLocation().Update(*location);
  }
  service_interval_map_[context_impl->GetDiscoverService().service_] =
      context_impl->GetDiscoverService().refresh_interval_;
  service_interval_map_[context_impl->GetMonitorService().service_] =
      context_impl->GetMonitorService().refresh_interval_;
  service_interval_map_[context_impl->GetHeartbeatService().service_] =
      context_impl->GetHeartbeatService().refresh_interval_;
  service_interval_map_[context_impl->GetMetricService().service_] = context_impl->GetMetricService().refresh_interval_;
  return kReturnOk;
}

void InMemoryRegistry::RunGcTask() {
  // 垃圾回收
  uint64_t rcu_min_time = context_->GetContextImpl()->RcuMinTime();
  uint64_t min_gc_time = rcu_min_time > 2000 ? rcu_min_time - 2000 : 0;
  service_instances_data_.CheckGc(min_gc_time);
  service_route_rule_data_.CheckGc(min_gc_time);
  service_rate_limit_data_.CheckGc(min_gc_time);
  service_circuit_breaker_config_data_.CheckGc(min_gc_time);
}

Service* InMemoryRegistry::CreateServiceInLock(const ServiceKey& service_key) {
  Service* service = nullptr;
  pthread_rwlock_wrlock(&rwlock_);
  std::map<ServiceKey, Service*>::iterator service_it = service_cache_.find(service_key);
  POLARIS_ASSERT(service_it == service_cache_.end())
  service = new Service(service_key, ++next_service_id_);
  service_cache_[service_key] = service;
  pthread_rwlock_unlock(&rwlock_);
  return service;
}

Service* InMemoryRegistry::GetServiceInLock(const ServiceKey& service_key) {
  Service* service = nullptr;
  pthread_rwlock_wrlock(&rwlock_);
  std::map<ServiceKey, Service*>::iterator service_it = service_cache_.find(service_key);
  if (service_it != service_cache_.end()) {
    service = service_it->second;
  }
  pthread_rwlock_unlock(&rwlock_);
  return service;
}

std::map<std::string, uint32_t> InMemoryRegistry::GetDynamicWeightDataWithLock(const ServiceKey& service_key) {
  std::map<std::string, uint32_t> dynamic_weight_data;
  pthread_rwlock_rdlock(&rwlock_);
  std::map<ServiceKey, Service*>::iterator service_it = service_cache_.find(service_key);
  if (service_it != service_cache_.end()) {
    dynamic_weight_data = service_it->second->GetDynamicWeightData();
  }
  pthread_rwlock_unlock(&rwlock_);
  return dynamic_weight_data;
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

void InMemoryRegistry::CheckExpireServiceData(uint64_t min_access_time, RcuMap<ServiceKey, ServiceData>& rcu_cache,
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
      context_impl->GetServerConnector()->DeregisterEventHandler(expired_services[i], service_data_type);
    }
    rcu_cache.Delete(expired_services[i]);
    context_impl->GetServiceRecord()->ServiceDataDelete(expired_services[i], service_data_type);
    context_impl->GetCacheManager()->GetCachePersist().PersistServiceData(expired_services[i], service_data_type, "");
    pthread_rwlock_unlock(&notify_rwlock_);
  }
}

void InMemoryRegistry::CheckExpireService(uint64_t min_access_time) {
  auto& service_context_map = context_->GetContextImpl()->GetServiceContextMap();
  std::vector<ServiceKey> expired_services;
  service_context_map.CheckExpired(min_access_time, expired_services);
  ServiceKeyWithType service_key_with_type;
  ContextImpl* context_impl = context_->GetContextImpl();
  for (std::size_t i = 0; i < expired_services.size(); ++i) {
    const ServiceKey& service_key = expired_services[i];
    service_key_with_type.service_key_ = service_key;
    service_key_with_type.data_type_ = kServiceDataInstances;
    pthread_rwlock_wrlock(&notify_rwlock_);
    // 删除服务实例
    if (service_data_notify_map_.erase(service_key_with_type) > 0) {  // 有通知对象表示注册过handler
      context_impl->GetServerConnector()->DeregisterEventHandler(service_key, kServiceDataInstances);
    }
    service_instances_data_.Delete(service_key);
    context_impl->GetServiceRecord()->ServiceDataDelete(service_key, kServiceDataInstances);
    context_impl->GetCacheManager()->GetCachePersist().PersistServiceData(service_key, kServiceDataInstances, "");

    // 删除路由规则
    service_key_with_type.data_type_ = kServiceDataRouteRule;
    if (service_data_notify_map_.erase(service_key_with_type) > 0) {  // 有通知对象表示注册过handler
      context_impl->GetServerConnector()->DeregisterEventHandler(service_key, kServiceDataRouteRule);
    }
    service_route_rule_data_.Delete(service_key);
    context_impl->GetServiceRecord()->ServiceDataDelete(service_key, kServiceDataRouteRule);
    context_impl->GetCacheManager()->GetCachePersist().PersistServiceData(service_key, kServiceDataRouteRule, "");
    pthread_rwlock_unlock(&notify_rwlock_);

    // 删除服务
    DeleteServiceInLock(expired_services[i]);
    // 删除 service_context
    service_context_map.Delete({service_key});
  }
}

void InMemoryRegistry::CheckAndSetExpireDynamicWeightServiceData(const ServiceKey& service_key) {
  Service* service = nullptr;
  pthread_rwlock_rdlock(&rwlock_);
  std::map<ServiceKey, Service*>::iterator service_it = service_cache_.find(service_key);
  if (service_it == service_cache_.end()) {
    pthread_rwlock_unlock(&rwlock_);
    return;
  }
  service = service_it->second;
  pthread_rwlock_unlock(&rwlock_);

  if (service->CheckAndSetDynamicWeightExpire()) {
    POLARIS_LOG(LOG_WARN, "service [%s/%s] remove expire dynamicweight data", service_key.namespace_.c_str(),
                service_key.name_.c_str());
    ServiceData* instances_service_data = service_instances_data_.Get(service_key);
    if (instances_service_data != nullptr && instances_service_data->IsAvailable()) {
      std::map<std::string, uint32_t> tmp;
      this->UpdateInstanceDynamicWeight(instances_service_data, tmp);
    }
  }
}

void InMemoryRegistry::RemoveExpireServiceData() {
  uint64_t min_access_time = Time::CoarseSteadyTimeSub(service_expire_time_);
  CheckExpireService(min_access_time);
  CheckExpireServiceData(min_access_time, service_rate_limit_data_, kServiceDataRateLimit);
  CheckExpireServiceData(min_access_time, service_circuit_breaker_config_data_, kCircuitBreakerConfig);
}

ReturnCode InMemoryRegistry::GetServiceDataWithRef(const ServiceKey& service_key, ServiceDataType data_type,
                                                   ServiceData*& service_data) {
  if (data_type == kServiceDataInstances) {
    ServiceContext* service_context = context_->GetContextImpl()->GetServiceContext(service_key);
    service_data = service_context->GetInstances();
    if (service_data == nullptr) {
      service_data = service_instances_data_.Get(service_key);
    } else {
      service_data->IncrementRef();
    }
  } else if (data_type == kServiceDataRouteRule) {
    service_data = service_route_rule_data_.Get(service_key);
  } else if (data_type == kServiceDataRateLimit) {
    service_data = service_rate_limit_data_.Get(service_key);
  } else if (data_type == kCircuitBreakerConfig) {
    service_data = service_circuit_breaker_config_data_.Get(service_key);
  }
  if (service_data == nullptr) {
    return kReturnServiceNotFound;
  }

  if (service_data->IsAvailable() || service_data->GetDataStatus() == kDataNotFound) {
    // 远端已经返回数据（包含服务不存在情况）或者磁盘数据未过期
    return kReturnOk;
  }

  return kReturnNotInit;
}

ServiceDataNotify* InMemoryRegistry::GetOrCreateDataNotify(const ServiceKey& service_key, ServiceDataType data_type,
                                                           bool& new_create) {
  new_create = false;
  ServiceDataNotify* data_notify = nullptr;
  ServiceKeyWithType service_key_with_type;
  service_key_with_type.service_key_ = service_key;
  service_key_with_type.data_type_ = data_type;
  std::map<ServiceKeyWithType, ServiceDataNotify*>::iterator notify_it =
      service_data_notify_map_.find(service_key_with_type);
  if (notify_it != service_data_notify_map_.end()) {
    data_notify = notify_it->second;
  } else {
    data_notify = new ServiceDataNotify(service_key, data_type);
    service_data_notify_map_[service_key_with_type] = data_notify;
    new_create = true;
  }
  POLARIS_ASSERT(data_notify != nullptr);
  return data_notify;
}

ReturnCode InMemoryRegistry::LoadServiceDataWithNotify(const ServiceKey& service_key, ServiceDataType data_type,
                                                       ServiceData*& service_data, ServiceDataNotify*& data_notify) {
  bool new_create = false;
  pthread_rwlock_wrlock(&notify_rwlock_);  // TODO 使用读写锁分离查询和创建操作
  data_notify = this->GetOrCreateDataNotify(service_key, data_type, new_create);
  if (new_create) {  // 只有首次创建注册更新任务
    ContextImpl* context_impl = context_->GetContextImpl();
    ServerConnector* server_connector = context_impl->GetServerConnector();
    ServiceEventHandler* handler = new ServiceEventHandlerImpl(this, data_notify);
    uint64_t refresh_interval = service_refresh_interval_;
    std::map<ServiceKey, uint64_t>::iterator interval_it = service_interval_map_.find(service_key);
    if (interval_it != service_interval_map_.end()) {
      refresh_interval = interval_it->second;
    }
    if (data_type == kServiceDataInstances) {
      CreateServiceInLock(service_key);
    }
    // 先加载磁盘缓存数据
    CachePersist& cache_persist = context_impl->GetCacheManager()->GetCachePersist();
    std::string disk_revision;
    ServiceData* disk_service_data = cache_persist.LoadServiceData(service_key, data_type);
    if (disk_service_data != nullptr) {
      this->UpdateServiceData(service_key, data_type, disk_service_data);
      if (service_data == nullptr) {
        disk_service_data->IncrementRef();
        service_data = disk_service_data;
      }
      if (disk_service_data->IsAvailable()) {
        data_notify->Notify(disk_service_data);
        disk_revision = disk_service_data->GetRevision();
      }
    }
    server_connector->RegisterEventHandler(service_key, data_type, refresh_interval, disk_revision, handler);
  }
  pthread_rwlock_unlock(&notify_rwlock_);
  if (new_create) {
    POLARIS_LOG(LOG_INFO, "load %s data with notify for service[%s/%s]", DataTypeToStr(data_type),
                service_key.namespace_.c_str(), service_key.name_.c_str());
  }
  return kReturnOk;
}

ReturnCode InMemoryRegistry::UpdateServiceData(const ServiceKey& service_key, ServiceDataType data_type,
                                               ServiceData* service_data) {
  Service* service = GetServiceInLock(service_key);
  if (service != nullptr) {  // 更新服务数据指向服务
    // 注意：这个函数里不能访问service的成员变量，且不能调用虚函数，否则会出现线程安全问题
    service->UpdateData(service_data);
  }
  ContextImpl* context_impl = context_->GetContextImpl();
  context_impl->RcuEnter();
  ServiceContext* service_context = context_impl->GetServiceContext(service_key);
  if (data_type == kServiceDataInstances) {
    if (service == nullptr) {  // 服务被反注册了
      if (service_data != nullptr) {
        service_data->DecrementRef();
      }
      context_impl->RcuExit();
      return kReturnOk;
    }
    ServiceData* old_service_data = service_instances_data_.Get(service_key);
    if (old_service_data != nullptr) {
      PluginManager::Instance().OnPreUpdateServiceData(old_service_data, service_data);
      old_service_data->DecrementRef();
    }

    service_instances_data_.Update(service_key, service_data);
    service_context->UpdateInstances(service_data);
  } else if (data_type == kServiceDataRouteRule) {
    if (service_data != nullptr) {  // 填充环境变量
      const SystemVariables& system_variables = context_impl->GetSystemVariables();
      service_data->GetServiceDataImpl()->FillSystemVariables(system_variables);
    }
    service_context->UpdateRoutings(service_data);
    service_route_rule_data_.Update(service_key, service_data);
  } else if (data_type == kServiceDataRateLimit) {
    service_rate_limit_data_.Update(service_key, service_data);
  } else if (data_type == kCircuitBreakerConfig) {
    service_circuit_breaker_config_data_.Update(service_key, service_data);
  } else {
    POLARIS_ASSERT(false);
  }
  if (service_data == nullptr) {  // Server Connector反注册Handler触发更新为NULL
    context_impl->RcuExit();
    return kReturnOk;
  }
  context_impl->GetServiceRecord()->ServiceDataUpdate(service_data);  // 同步记录Service版本变化
  if (service_data->GetDataStatus() == kDataInitFromDisk) {
    context_impl->RcuExit();
    return kReturnOk;  // 磁盘数据无需回写磁盘缓存
  }
  context_impl->GetCacheManager()->SubmitServiceDataChange(service_data);
  if (service_data->GetDataStatus() == kDataNotFound) {
    // 服务不存在的数据不存入本地缓存，则尝试删除之前的缓存
    context_impl->GetCacheManager()->GetCachePersist().PersistServiceData(service_key, data_type, "");
  } else {
    context_impl->GetCacheManager()->GetCachePersist().PersistServiceData(
        service_data->GetServiceKey(), service_data->GetDataType(), service_data->ToJsonString());
  }
  context_impl->RcuExit();
  return kReturnOk;
}

ReturnCode InMemoryRegistry::UpdateServiceSyncTime(const ServiceKey& service_key, ServiceDataType data_type) {
  ContextImpl* context_impl = context_->GetContextImpl();
  context_impl->GetCacheManager()->GetCachePersist().UpdateSyncTime(service_key, data_type);
  return kReturnOk;
}

ReturnCode InMemoryRegistry::UpdateCircuitBreakerData(const ServiceKey& service_key,
                                                      const CircuitBreakerData& circuit_breaker_data) {
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

ReturnCode InMemoryRegistry::UpdateDynamicWeight(const ServiceKey& service_key,
                                                 const DynamicWeightData& dynamic_weight_data) {
  Service* service = nullptr;
  pthread_rwlock_rdlock(&rwlock_);
  std::map<ServiceKey, Service*>::iterator service_it = service_cache_.find(service_key);
  if (service_it == service_cache_.end()) {
    pthread_rwlock_unlock(&rwlock_);
    POLARIS_LOG(LOG_WARN, "Update dynamic_weight status failed because service[%s/%s] not found",
                service_key.namespace_.c_str(), service_key.name_.c_str());
    return kReturnOk;
  }
  service = service_it->second;
  pthread_rwlock_unlock(&rwlock_);

  bool status_change = false;
  service->SetDynamicWeightData(dynamic_weight_data, status_change);

  if (dynamic_weight_data.status == kDynamicWeightUpdating || status_change) {
    ServiceData* instances_service_data = service_instances_data_.Get(service_key);
    if (instances_service_data != nullptr && instances_service_data->IsAvailable()) {
      this->UpdateInstanceDynamicWeight(instances_service_data, dynamic_weight_data.dynamic_weights);
    }
  }

  return kReturnOk;
}

ReturnCode InMemoryRegistry::UpdateInstanceDynamicWeight(ServiceData* instances_service_data,
                                                         const std::map<std::string, uint32_t>& dynamic_weights) {
  if (instances_service_data == nullptr || !instances_service_data->IsAvailable() ||
      instances_service_data->GetDataType() != kServiceDataInstances) {
    return kReturnNotInit;
  }

  ServiceKey service_key = instances_service_data->GetServiceKey();
  POLARIS_LOG(LOG_DEBUG, "[%s/%s] update instance dynamicweight, map size:%d", service_key.namespace_.c_str(),
              service_key.name_.c_str(), (int)dynamic_weights.size());

  std::map<std::string, Instance*>& instances =
      instances_service_data->GetServiceDataImpl()->data_.instances_->instances_map_;
  for (std::map<std::string, Instance*>::iterator it = instances.begin(); it != instances.end(); ++it) {
    Instance* inst = it->second;
    std::ostringstream dy_key;
    dy_key << inst->GetHost() << ":" << inst->GetPort() << ":" << inst->GetVpcId();
    std::map<std::string, uint32_t>::const_iterator dy_it = dynamic_weights.find(dy_key.str());
    if (dy_it != dynamic_weights.end()) {
      inst->GetImpl().SetDynamicWeight(dy_it->second);
    } else {
      //两种情况都使用静态兜底：
      // 1.动态权重上报缺失。
      // 2.参数传进来的map本身就是空，只是为了强刷已经生成的instances数据，
      //一般发生动态权重的svr挂掉或者异常时候传入空对象触发。
      inst->GetImpl().SetDynamicWeight(inst->GetWeight());
    }
  }

  std::set<Instance*>& isolate_instances =
      instances_service_data->GetServiceDataImpl()->data_.instances_->isolate_instances_;
  for (std::set<Instance*>::iterator it = isolate_instances.begin(); it != isolate_instances.end(); ++it) {
    Instance* inst = *it;
    std::ostringstream dy_key;
    dy_key << inst->GetHost() << ":" << inst->GetPort() << ":" << inst->GetVpcId();
    std::map<std::string, uint32_t>::const_iterator dy_it = dynamic_weights.find(dy_key.str());
    if (dy_it != dynamic_weights.end()) {
      inst->GetImpl().SetDynamicWeight(dy_it->second);
    } else {
      inst->GetImpl().SetDynamicWeight(inst->GetWeight());
    }
  }

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

ReturnCode InMemoryRegistry::UpdateSetCircuitBreakerData(const ServiceKey& service_key,
                                                         const CircuitBreakUnhealthySetsData& unhealthy_sets) {
  Service* service;
  pthread_rwlock_rdlock(&rwlock_);
  std::map<ServiceKey, Service*>::iterator service_it = service_cache_.find(service_key);
  if (service_it == service_cache_.end()) {
    pthread_rwlock_unlock(&rwlock_);
    POLARIS_LOG(LOG_WARN, "Update set circuit breaker status failed because service[%s/%s] not found",
                service_key.namespace_.c_str(), service_key.name_.c_str());
    return kReturnOk;
  }
  service = service_it->second;
  pthread_rwlock_unlock(&rwlock_);
  return service->WriteCircuitBreakerUnhealthySets(unhealthy_sets);
}

ReturnCode InMemoryRegistry::GetCircuitBreakerInstances(const ServiceKey& service_key, ServiceData*& service_data,
                                                        std::vector<Instance*>& open_instances) {
  service_data = service_instances_data_.Get(service_key, false);
  if (service_data == nullptr) {
    return kReturnServiceNotFound;
  }
  if (!service_data->IsAvailable()) {
    service_data->DecrementRef();
    return kReturnServiceNotFound;
  }
  // 由于此处获取service data没有更新访问时间，服务可能淘汰，不能直接使用其关联的服务数据
  pthread_rwlock_rdlock(&rwlock_);
  std::map<ServiceKey, Service*>::iterator service_it = service_cache_.find(service_key);
  if (service_it == service_cache_.end()) {
    pthread_rwlock_unlock(&rwlock_);
    return kReturnServiceNotFound;
  }
  std::set<std::string> open_instance = service_it->second->GetCircuitBreakerOpenInstances();
  pthread_rwlock_unlock(&rwlock_);

  ServiceInstances service_instances(service_data);
  std::map<std::string, Instance*>& instance_map = service_instances.GetInstances();
  for (std::set<std::string>::iterator it = open_instance.begin(); it != open_instance.end(); ++it) {
    const std::string& instance_id = *it;
    std::map<std::string, Instance*>::iterator iter = instance_map.find(instance_id);
    if (iter == instance_map.end()) {
      POLARIS_LOG(LOG_INFO, "The health checker of service[%s/%s] getting instance[%s] failed",
                  service_key.namespace_.c_str(), service_key.name_.c_str(), instance_id.c_str());
      continue;
    }
    open_instances.push_back(iter->second);
  }
  return kReturnOk;
}

}  // namespace polaris
