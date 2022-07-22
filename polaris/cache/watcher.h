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

#ifndef POLARIS_CPP_POLARIS_CACHE_WATCHER_H_
#define POLARIS_CPP_POLARIS_CACHE_WATCHER_H_

#include "api/consumer_api.h"
#include "polaris/model.h"
#include "reactor/task.h"
#include "utils/ref_count.h"

namespace polaris {

/// @brief 抽象缓存监听接口，用于监听本地缓存的变化
class Watcher {
 public:
  virtual ~Watcher() {}

  virtual void Notify() = 0;
};

enum WaitDataType {  // 等待服务数据类型
  kWaitDataNone = 0,
  kWaitDataDstInstances = 1,
  kWaitDataDstRuleRouter = 1 << 1,
  kWaitDataSrcRuleRouter = 1 << 2
};

/// @brief 一定时间内监听本地缓存是否加载
class TimeoutWatcher : public RefCount {
 public:
  TimeoutWatcher(InstancesFuture::Impl* future_impl, ServiceCacheNotify* service_cache_notify);

  virtual ~TimeoutWatcher();

  void NotifyReady(const ServiceKey& service_key, ServiceDataType data_type);

  void NotifyTimeout();

  static void SetupTimeoutTask(TimeoutWatcher* timeout_watcher);

  static void ServiceCacheTimeout(TimeoutWatcher* timeout_watcher);

 private:
  friend class CacheManager;
  InstancesFuture::Impl* future_impl_;
  ServiceCacheNotify* service_cache_notify_;
  int wait_data_flag;
  TimingTaskIter timeout_task_iter_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CACHE_WATCHER_H_
