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

#ifndef POLARIS_CPP_POLARIS_CACHE_CACHE_MANAGER_H_
#define POLARIS_CPP_POLARIS_CACHE_CACHE_MANAGER_H_

#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "cache/rcu_map.h"
#include "cache_persist.h"
#include "engine/executor.h"
#include "model/model_impl.h"
#include "polaris/consumer.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "reactor/task.h"
#include "report_client.h"

namespace polaris {

class TimeoutWatcher;
class Watcher;

// 管理服务级别的Watch
struct ServiceDataWatchers {
  std::set<TimeoutWatcher*> timeout_watchers_;
  std::vector<Watcher*> watchers_;
};

class CacheManager;
struct WatherRegisterTask {
  ServiceKey service_key_;
  CacheManager* cache_manager_;
  Watcher* wather_;
};

class ServiceDataChangeTask : public Task {
 public:
  ServiceDataChangeTask(CacheManager* cache_manager, ServiceData* service_data);
  virtual ~ServiceDataChangeTask();

  virtual void Run();

 private:
  CacheManager* cache_manager_;
  ServiceData* service_data_;
};

struct InstanceHostPortKey {
  std::string host_;
  int port_;
};

inline bool operator<(InstanceHostPortKey const& lhs, InstanceHostPortKey const& rhs) {
  if (lhs.port_ == rhs.port_) {
    return lhs.host_ < rhs.host_;
  } else {
    return lhs.port_ < rhs.port_;
  }
}

class ServiceHostPort : public ServiceBase {
 public:
  uint64_t version_;
  std::map<InstanceHostPortKey, std::string> mapping_;
};

/// @brief 用于管理本地缓存，一个Context初始化一个缓冲管理对象
class CacheManager : public Executor {
 public:
  explicit CacheManager(Context* context);
  virtual ~CacheManager();

  virtual const char* GetName() { return "cache_mgr"; }

  virtual void SetupWork();  // 设置定时任务

  /// @brief 提供给Consumer调用的接口，用于注册未就绪的InstancesFuture，线程安全
  void RegisterTimeoutWatcher(InstancesFuture::Impl* future_impl, ServiceCacheNotify* service_cache_notify);

  void RemoveTimeoutWatcher(TimeoutWatcher* timeout_watcher);

  // 提交服务变更事件
  void SubmitServiceDataChange(ServiceData* service_data);

  // 处理服务数据变更事件
  void OnServiceDataChange(ServiceData* service_data);

  CachePersist& GetCachePersist() { return persist_; }

  // 获取服务实例的
  ReturnCode GetInstanceId(const ServiceKey& service_key, const InstanceHostPortKey& host_port_key,
                           std::string& instance_id);

 private:
  // 定时清理不使用的插件缓存
  static void TimingClearCache(CacheManager* cache_manager);

  static void TimingLocalRegistryTask(CacheManager* cache_manager);

  // 在当前线程线程添加Watcher
  static void AddTimeoutWatcher(TimeoutWatcher* timeout_watcher);

  ReturnCode GetOrCreateServiceHostPort(const ServiceKey& service_key, ServiceHostPort*& host_port_data);

 private:
  CachePersist persist_;
  ReportClient report_client_;
  std::map<ServiceKeyWithType, ServiceDataWatchers> service_watchers_;

  RcuMap<ServiceKey, ServiceHostPort> host_port_cache_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CACHE_CACHE_MANAGER_H_
