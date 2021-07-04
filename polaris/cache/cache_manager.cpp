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

#include "cache_manager.h"

#include <stddef.h>

#include <utility>

#include "api/consumer_api.h"
#include "cache/watcher.h"
#include "context_internal.h"
#include "polaris/consumer.h"
#include "polaris/context.h"
#include "polaris/plugin.h"
#include "reactor/reactor.h"
#include "utils/time_clock.h"

namespace polaris {

ServiceDataChangeTask::ServiceDataChangeTask(CacheManager* cache_manager, ServiceData* service_data)
    : cache_manager_(cache_manager), service_data_(service_data) {
  service_data_->IncrementRef();
}
ServiceDataChangeTask::~ServiceDataChangeTask() {
  if (service_data_ != NULL) {
    service_data_->DecrementRef();
  }
}

void ServiceDataChangeTask::Run() {
  cache_manager_->OnServiceDataChange(service_data_);
  service_data_ = NULL;
}

CacheManager::CacheManager(Context* context) : Executor(context), persist_(reactor_) {}

CacheManager::~CacheManager() {
  std::map<ServiceKeyWithType, ServiceDataWatchers>::iterator watcher_it;
  for (watcher_it = service_watchers_.begin(); watcher_it != service_watchers_.end();
       ++watcher_it) {
    ServiceDataWatchers& watchers = watcher_it->second;
    for (std::size_t i = 0; i < watchers.watchers_.size(); ++i) {
      delete watchers.watchers_[i];
    }
    for (std::set<TimeoutWatcher*>::iterator it = watchers.timeout_watchers_.begin();
         it != watchers.timeout_watchers_.end(); ++it) {
      (*it)->DecrementRef();
    }
  }
}

void CacheManager::SetupWork() {
  // 设置定时清理任务
  reactor_.AddTimingTask(new TimingFuncTask<CacheManager>(TimingClearCache, this, 2000));
  reactor_.AddTimingTask(new TimingFuncTask<CacheManager>(TimingLocalRegistryTask, this, 2000));
}

void CacheManager::TimingClearCache(CacheManager* cache_manager) {
  cache_manager->context_->GetContextImpl()->ClearCache();

  // 清除缓存的host:port -> instance id的索引缓存
  std::vector<ServiceKey> clear_keys;
  cache_manager->host_port_cache_.CheckExpired(Time::GetCurrentTimeMs() - 5000, clear_keys);
  for (std::size_t i = 0; i < clear_keys.size(); ++i) {
    cache_manager->host_port_cache_.Delete(clear_keys[i]);
  }
  cache_manager->host_port_cache_.CheckGc(Time::GetCurrentTimeMs() - 1000);

  cache_manager->reactor_.AddTimingTask(
      new TimingFuncTask<CacheManager>(TimingClearCache, cache_manager, 2000));
}

void CacheManager::TimingLocalRegistryTask(CacheManager* cache_manager) {
  LocalRegistry* local_registry = cache_manager->context_->GetLocalRegistry();
  local_registry->RunGcTask();
  local_registry->RemoveExpireServiceData(Time::GetCurrentTimeMs());
  cache_manager->reactor_.AddTimingTask(
      new TimingFuncTask<CacheManager>(TimingLocalRegistryTask, cache_manager, 2000));
}

void CacheManager::RegisterTimeoutWatcher(InstancesFutureImpl* future_impl,
                                          ServiceCacheNotify* service_cache_notify) {
  future_impl->IncrementRef();
  TimeoutWatcher* timeout_watcher = new TimeoutWatcher(future_impl, service_cache_notify);
  reactor_.SubmitTask(new FuncTask<TimeoutWatcher>(AddTimeoutWatcher, timeout_watcher));
}

void CacheManager::AddTimeoutWatcher(TimeoutWatcher* timeout_watcher) {
  InstancesFutureImpl* future_impl = timeout_watcher->future_impl_;
  if (future_impl->route_info_notify_->IsDataReady(false)) {  // 数据已经就绪，直接触发通知返回
    timeout_watcher->service_cache_notify_->NotifyReady();
    delete timeout_watcher;
    return;
  }
  CacheManager* cache_manager = future_impl->context_impl_->GetCacheManager();
  // 每种等待的数据保持监听索引
  ServiceKeyWithType service_key_with_type;
  service_key_with_type.service_key_ = future_impl->route_info_.GetServiceKey();
  service_key_with_type.data_type_   = kServiceDataInstances;
  cache_manager->service_watchers_[service_key_with_type].timeout_watchers_.insert(timeout_watcher);
  timeout_watcher->wait_data_flag |= kWaitDataDstInstances;
  if (future_impl->service_context_->GetServiceRouterChain()->IsRuleRouterEnable()) {
    service_key_with_type.data_type_ = kServiceDataRouteRule;
    timeout_watcher->IncrementRef();
    timeout_watcher->wait_data_flag |= kWaitDataDstRuleRouter;
    cache_manager->service_watchers_[service_key_with_type].timeout_watchers_.insert(
        timeout_watcher);
    ServiceInfo* src_service_info = future_impl->route_info_.GetSourceServiceInfo();
    if (src_service_info != NULL && !src_service_info->service_key_.name_.empty()) {
      service_key_with_type.service_key_ = src_service_info->service_key_;
      timeout_watcher->IncrementRef();
      timeout_watcher->wait_data_flag |= kWaitDataSrcRuleRouter;
      cache_manager->service_watchers_[service_key_with_type].timeout_watchers_.insert(
          timeout_watcher);
    }
  }
  TimeoutWatcher::SetupTimeoutTask(timeout_watcher);
}

void CacheManager::RemoveTimeoutWatcher(TimeoutWatcher* timeout_watcher) {
  ServiceKeyWithType service_key_with_type;
  if (timeout_watcher->wait_data_flag & kWaitDataDstInstances) {
    service_key_with_type.data_type_   = kServiceDataInstances;
    service_key_with_type.service_key_ = timeout_watcher->future_impl_->route_info_.GetServiceKey();
    service_watchers_[service_key_with_type].timeout_watchers_.erase(timeout_watcher);
    timeout_watcher->DecrementRef();
  }
  if (timeout_watcher->wait_data_flag & kWaitDataDstRuleRouter) {
    service_key_with_type.data_type_   = kServiceDataRouteRule;
    service_key_with_type.service_key_ = timeout_watcher->future_impl_->route_info_.GetServiceKey();
    service_watchers_[service_key_with_type].timeout_watchers_.erase(timeout_watcher);
    timeout_watcher->DecrementRef();
  }
  if (timeout_watcher->wait_data_flag & kWaitDataSrcRuleRouter) {
    service_key_with_type.data_type_ = kServiceDataRouteRule;
    service_key_with_type.service_key_ =
        timeout_watcher->future_impl_->route_info_.GetSourceServiceInfo()->service_key_;
    service_watchers_[service_key_with_type].timeout_watchers_.erase(timeout_watcher);
    timeout_watcher->DecrementRef();
  }
}

void CacheManager::SubmitServiceDataChange(ServiceData* service_data) {
  reactor_.SubmitTask(new ServiceDataChangeTask(this, service_data));
}

void CacheManager::OnServiceDataChange(ServiceData* service_data) {
  ServiceKeyWithType service_key_with_type;
  service_key_with_type.service_key_ = service_data->GetServiceKey();
  service_key_with_type.data_type_   = service_data->GetDataType();
  std::map<ServiceKeyWithType, ServiceDataWatchers>::iterator watchers_it =
      service_watchers_.find(service_key_with_type);
  if (watchers_it == service_watchers_.end()) {
    service_data->DecrementRef();
    return;
  }

  // 处理 timeout watcher
  std::set<TimeoutWatcher*>& timeout_wathers = watchers_it->second.timeout_watchers_;
  for (std::set<TimeoutWatcher*>::iterator it = timeout_wathers.begin();
       it != timeout_wathers.end(); ++it) {
    (*it)->NotifyReady(service_data->GetServiceKey(), service_data->GetDataType());
    (*it)->DecrementRef();
  }
  timeout_wathers.clear();

  service_data->DecrementRef();
  if (watchers_it->second.timeout_watchers_.empty()) {
    service_watchers_.erase(watchers_it);
  }
}

ReturnCode CacheManager::GetOrCreateServiceHostPort(const ServiceKey& service_key,
                                                    ServiceHostPort*& host_port_data) {
  LocalRegistry* local_register = context_->GetLocalRegistry();
  ServiceData* service_data     = NULL;
  ReturnCode ret_code;
  if ((ret_code = local_register->GetServiceDataWithRef(service_key, kServiceDataInstances,
                                                        service_data)) != kReturnOk) {
    if (host_port_data != NULL) {
      host_port_data->DecrementRef();
    }
    return ret_code;
  }
  if (host_port_data != NULL) {
    if (host_port_data->version_ >= service_data->GetCacheVersion()) {
      host_port_data->DecrementRef();
      return kReturnInstanceNotFound;
    }
    host_port_data->DecrementRef();
  }
  host_port_data           = new ServiceHostPort();
  host_port_data->version_ = service_data->GetCacheVersion();
  ServiceInstances service_instances(service_data);
  std::map<std::string, Instance*> instances = service_instances.GetInstances();
  std::map<std::string, Instance*>::iterator it;
  for (it = instances.begin(); it != instances.end(); ++it) {
    InstanceHostPortKey key = {it->second->GetHost(), it->second->GetPort()};
    host_port_data->mapping_.insert(std::make_pair(key, it->first));
  }
  host_port_data->IncrementRef();
  host_port_cache_.Update(service_key, host_port_data);
  return kReturnOk;
}

ReturnCode CacheManager::GetInstanceId(const ServiceKey& service_key, const std::string& host,
                                       int port, std::string& instance_id) {
  ServiceHostPort* host_port_data = host_port_cache_.Get(service_key);
  InstanceHostPortKey key         = {host, port};
  std::map<InstanceHostPortKey, std::string>::iterator it;
  if (host_port_data != NULL) {
    if ((it = host_port_data->mapping_.find(key)) != host_port_data->mapping_.end()) {
      instance_id = it->second;
      host_port_data->DecrementRef();
      return kReturnOk;
    }
  }
  ReturnCode ret_code;
  if ((ret_code = GetOrCreateServiceHostPort(service_key, host_port_data)) != kReturnOk) {
    return ret_code;
  }
  // 拿到更新数据后再找一次
  if ((it = host_port_data->mapping_.find(key)) != host_port_data->mapping_.end()) {
    instance_id = it->second;
    host_port_data->DecrementRef();
    return kReturnOk;
  }
  host_port_data->DecrementRef();
  return kReturnInstanceNotFound;
}

}  // namespace polaris
