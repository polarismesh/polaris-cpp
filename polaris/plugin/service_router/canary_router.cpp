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

#include "canary_router.h"

#include <stddef.h>

#include <map>
#include <utility>

#include "cache/service_cache.h"
#include "context_internal.h"
#include "logger.h"
#include "model/constants.h"
#include "model/model_impl.h"
#include "monitor/service_record.h"
#include "polaris/context.h"
#include "polaris/model.h"
#include "service_router.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

class Config;

CanaryServiceRouter::CanaryServiceRouter() : context_(NULL), router_cache_(NULL) {}

CanaryServiceRouter::~CanaryServiceRouter() {
  context_ = NULL;
  if (router_cache_ != NULL) {
    router_cache_->SetClearHandler(0);
    router_cache_->DecrementRef();
    router_cache_ = NULL;
  }
}

ReturnCode CanaryServiceRouter::Init(Config* /*config*/, Context* context) {
  context_      = context;
  router_cache_ = new ServiceCache<CanaryCacheKey>();
  context->GetContextImpl()->RegisterCache(router_cache_);
  return kReturnOk;
}

ReturnCode CanaryServiceRouter::DoRoute(RouteInfo& route_info, RouteResult* route_result) {
  POLARIS_CHECK_ARGUMENT(route_result != NULL);

  ServiceInstances* service_instances = route_info.GetServiceInstances();
  POLARIS_CHECK_ARGUMENT(service_instances != NULL);
  // 查询服务是否通过元数据配置开启金丝雀路由
  if (!service_instances->IsCanaryEnable()) {
    route_result->SetServiceInstances(service_instances);
    route_info.SetServiceInstances(NULL);
    return kReturnOk;
  }

  ContextImpl* context_impl     = context_->GetContextImpl();
  const ServiceKey& service_key = service_instances->GetServiceData()->GetServiceKey();

  // 优先查询缓存
  CanaryCacheKey cache_key;
  cache_key.prior_data_ = service_instances->GetAvailableInstances();
  cache_key.circuit_breaker_version_ =
      service_instances->GetService()->GetCircuitBreakerDataVersion();

  // 查找canary的值
  if (route_info.GetSourceServiceInfo() != NULL) {
    std::map<std::string, std::string>& metadata = route_info.GetSourceServiceInfo()->metadata_;
    std::map<std::string, std::string>::iterator metadata_it;
    if ((metadata_it = metadata.find(constants::kRouterRequestCanaryKey)) != metadata.end()) {
      cache_key.canary_value_ = metadata_it->second;
    }
  }

  CacheValueBase* cache_value_base = router_cache_->GetWithRef(cache_key);
  RouterSubsetCache* cache_value   = NULL;
  if (cache_value_base != NULL) {
    cache_value = dynamic_cast<RouterSubsetCache*>(cache_value_base);
    POLARIS_ASSERT(cache_value != NULL);
  } else {
    InstancesSet* prior_result = cache_key.prior_data_;
    std::set<Instance*> unhealthy_set;
    CalculateUnhealthySet(route_info, service_instances, unhealthy_set);
    std::vector<Instance*> result;
    bool recover_all = false;
    if (cache_key.canary_value_.empty()) {
      recover_all = CalculateResult(prior_result->GetInstances(), unhealthy_set, result);
    } else {
      recover_all = CalculateResult(prior_result->GetInstances(), cache_key.canary_value_,
                                    unhealthy_set, result);
    }
    cache_value                  = new RouterSubsetCache();
    cache_value->instances_data_ = service_instances->GetServiceData();
    cache_value->instances_data_->IncrementRef();
    std::map<std::string, std::string> subset;
    subset["canary"] = cache_key.canary_value_;
    if (recover_all) {
      cache_value->current_data_ = new InstancesSet(result, subset, cache_key.canary_value_);
      if (!prior_result->recover_all_ && prior_result->recover_all_.Cas(false, true)) {
        context_impl->GetServiceRecord()->InstanceRecoverAll(
            service_key,
            new RecoverAllRecord(Time::GetCurrentTimeMs(), cache_key.canary_value_, true));
      }
    } else {
      cache_value->current_data_ = new InstancesSet(result, subset);
      if (prior_result->recover_all_ && prior_result->recover_all_.Cas(true, false)) {
        context_impl->GetServiceRecord()->InstanceRecoverAll(
            service_key,
            new RecoverAllRecord(Time::GetCurrentTimeMs(), cache_key.canary_value_, false));
      }
    }
    router_cache_->PutWithRef(cache_key, cache_value);
  }
  cache_value->current_data_->count_++;
  service_instances->UpdateAvailableInstances(cache_value->current_data_);
  cache_value->DecrementRef();
  route_result->SetServiceInstances(service_instances);
  route_info.SetServiceInstances(NULL);
  return kReturnOk;
}

RouterStatData* CanaryServiceRouter::CollectStat() { return router_cache_->CollectStat(); }

bool CanaryServiceRouter::CalculateResult(const std::vector<Instance*>& instances,
                                          const std::set<Instance*>& unhealthy_set,
                                          std::vector<Instance*>& result) {
  std::vector<Instance*> select_healthy;
  std::vector<Instance*> select_unhealthy;
  std::vector<Instance*> other_healthy;
  std::vector<Instance*> other_unhealthy;
  for (std::size_t i = 0; i < instances.size(); ++i) {
    Instance* const& instance = instances[i];
    if (instance->GetMetadata().count("canary") == 0) {
      if (unhealthy_set.count(instance) == 0) {
        select_healthy.push_back(instance);
      } else {
        select_unhealthy.push_back(instance);
      }
    } else {
      if (unhealthy_set.count(instance) == 0) {
        other_healthy.push_back(instance);
      } else {
        other_unhealthy.push_back(instance);
      }
    }
  }
  if (!select_healthy.empty()) {  // 1. 选择非金丝雀健康实例
    result.swap(select_healthy);
    return false;
  } else if (!other_healthy.empty()) {  // 2. 降级金丝雀健康实例
    result.swap(other_healthy);
  } else if (!select_unhealthy.empty()) {  // 3. 降级非金丝雀非健康实例
    result.swap(select_unhealthy);
  } else if (!other_unhealthy.empty()) {  // 4. 降级金丝雀非健康实例
    result.swap(other_unhealthy);
  }
  return !result.empty();
}

bool CanaryServiceRouter::CalculateResult(const std::vector<Instance*>& instances,
                                          const std::string& canary_value,
                                          const std::set<Instance*>& unhealthy_set,
                                          std::vector<Instance*>& result) {
  std::vector<Instance*> select_healthy;    // 选中的金丝雀健康节点
  std::vector<Instance*> select_unhealthy;  // 选中的金丝雀非健康节点
  std::vector<Instance*> normal_healthy;    // 非金丝雀健康节点
  std::vector<Instance*> normal_unhealthy;  // 非金丝雀非健康节点
  std::vector<Instance*> other_healthy;     // 其他金丝雀健康节点
  std::vector<Instance*> other_unhealthy;   // 其他金丝雀非健康节点
  for (std::size_t i = 0; i < instances.size(); ++i) {
    Instance* const& instance                       = instances[i];
    std::map<std::string, std::string>::iterator it = instance->GetMetadata().find("canary");
    if (it != instance->GetMetadata().end()) {
      if (it->second == canary_value) {
        if (unhealthy_set.count(instance) == 0) {
          select_healthy.push_back(instance);
        } else {
          select_unhealthy.push_back(instance);
        }
      } else {
        if (unhealthy_set.count(instance) == 0) {
          other_healthy.push_back(instance);
        } else {
          other_unhealthy.push_back(instance);
        }
      }
    } else {
      if (unhealthy_set.count(instance) == 0) {
        normal_healthy.push_back(instance);
      } else {
        normal_unhealthy.push_back(instance);
      }
    }
  }
  if (!select_healthy.empty()) {  // 1. 返回对应金丝雀健康节点
    result.swap(select_healthy);
    return false;
  } else if (!normal_healthy.empty()) {  // 2. 降级到非金丝雀健康节点
    result.swap(normal_healthy);
  } else if (!other_healthy.empty()) {  // 3. 降级到其他金丝雀健康节点
    result.swap(other_healthy);
  } else if (!select_unhealthy.empty()) {  // 4. 降级到金丝雀非健康节点
    result.swap(select_unhealthy);
  } else if (!normal_unhealthy.empty()) {  // 5. 降级到非金丝雀非健康节点
    result.swap(normal_unhealthy);
  } else if (!other_unhealthy.empty()) {  // 6. 降级到其他金丝雀非健康节点
    result.swap(other_unhealthy);
  }
  return !result.empty();
}

}  // namespace polaris
