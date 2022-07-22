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

#include "watcher.h"

#include <stddef.h>
#include <string>

#include "cache_manager.h"
#include "context/context_impl.h"
#include "logger.h"
#include "polaris/defs.h"
#include "reactor/reactor.h"

namespace polaris {

TimeoutWatcher::TimeoutWatcher(InstancesFuture::Impl* future_impl, ServiceCacheNotify* service_cache_notify)
    : future_impl_(future_impl), service_cache_notify_(service_cache_notify), wait_data_flag(kWaitDataNone) {}

TimeoutWatcher::~TimeoutWatcher() {
  if (future_impl_ != nullptr) {
    future_impl_->DecrementRef();
    future_impl_ = nullptr;
  }
  if (service_cache_notify_ != nullptr) {
    delete service_cache_notify_;
    service_cache_notify_ = nullptr;
  }
}

void TimeoutWatcher::NotifyReady(const ServiceKey& service_key, ServiceDataType data_type) {
  if (data_type == kServiceDataInstances) {
    POLARIS_ASSERT((wait_data_flag & kWaitDataDstInstances) != 0);
    wait_data_flag = wait_data_flag ^ kWaitDataDstInstances;
  } else if (data_type == kServiceDataRouteRule) {
    const ServiceKey& route_service_key = future_impl_->route_info_.GetServiceKey();
    if (route_service_key == service_key) {
      POLARIS_ASSERT((wait_data_flag & kWaitDataDstRuleRouter) != 0);
      wait_data_flag = wait_data_flag ^ kWaitDataDstRuleRouter;
    } else {
      POLARIS_ASSERT((wait_data_flag & kWaitDataSrcRuleRouter) != 0);
      wait_data_flag = wait_data_flag ^ kWaitDataSrcRuleRouter;
    }
  } else {
    POLARIS_ASSERT(false);
  }
  if (wait_data_flag == kWaitDataNone) {
    CacheManager* cache_manager = future_impl_->context_impl_->GetCacheManager();
    Reactor& reactor = cache_manager->GetReactor();
    reactor.CancelTimingTask(timeout_task_iter_);
    service_cache_notify_->NotifyReady();
  }
}

void TimeoutWatcher::NotifyTimeout() { service_cache_notify_->NotifyTimeout(); }

void TimeoutWatcher::SetupTimeoutTask(TimeoutWatcher* timeout_watcher) {
  CacheManager* cache_manager = timeout_watcher->future_impl_->context_impl_->GetCacheManager();
  Reactor& reactor = cache_manager->GetReactor();
  timeout_watcher->timeout_task_iter_ = reactor.AddTimingTask(new TimingFuncTask<TimeoutWatcher>(
      TimeoutWatcher::ServiceCacheTimeout, timeout_watcher, timeout_watcher->future_impl_->request_timeout_));
}

void TimeoutWatcher::ServiceCacheTimeout(TimeoutWatcher* timeout_watcher) {
  CacheManager* cache_manager = timeout_watcher->future_impl_->context_impl_->GetCacheManager();
  timeout_watcher->NotifyTimeout();
  cache_manager->RemoveTimeoutWatcher(timeout_watcher);
}

}  // namespace polaris
