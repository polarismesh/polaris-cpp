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

#include "metadata_router.h"

#include <stddef.h>

#include <utility>

#include "cache/service_cache.h"
#include "context_internal.h"
#include "logger.h"
#include "model/model_impl.h"
#include "monitor/service_record.h"
#include "polaris/context.h"
#include "polaris/model.h"
#include "service_router.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

class Config;

MetadataServiceRouter::MetadataServiceRouter() : context_(NULL), router_cache_(NULL) {}

MetadataServiceRouter::~MetadataServiceRouter() {
  context_ = NULL;
  if (router_cache_ != NULL) {
    router_cache_->SetClearHandler(0);
    router_cache_->DecrementRef();
    router_cache_ = NULL;
  }
}

ReturnCode MetadataServiceRouter::Init(Config* /*config*/, Context* context) {
  context_      = context;
  router_cache_ = new ServiceCache<MetadataCacheKey>();
  context->GetContextImpl()->RegisterCache(router_cache_);
  return kReturnOk;
}

static bool MetadataMatch(const std::map<std::string, std::string>& metadata,
                          const std::map<std::string, std::string>& instance_metadata) {
  if (metadata.size() > instance_metadata.size()) {
    return false;
  }
  std::map<std::string, std::string>::const_iterator it;
  std::map<std::string, std::string>::const_iterator instance_it;
  for (it = metadata.begin(); it != metadata.end(); ++it) {
    instance_it = instance_metadata.find(it->first);
    if (instance_it == instance_metadata.end() || it->second != instance_it->second) {
      return false;
    }
  }
  return true;
}

bool MetadataServiceRouter::CalculateResult(const std::vector<Instance*>& instances,
                                            const std::set<Instance*>& unhealthy_set,
                                            const std::map<std::string, std::string>& metadata,
                                            MetadataFailoverType failover_type,
                                            std::vector<Instance*>& result) {
  std::vector<Instance*> unhealthy;
  for (std::size_t i = 0; i < instances.size(); ++i) {
    Instance* const& instance = instances[i];
    if (MetadataMatch(metadata, instance->GetMetadata())) {
      if (unhealthy_set.count(instance) == 0) {
        result.push_back(instance);
      } else {
        unhealthy.push_back(instance);
      }
    }
  }
  if (!result.empty()) {
    return false;
  }
  if (!unhealthy.empty()) {
    result.swap(unhealthy);
    return true;
  }
  if (failover_type == kMetadataFailoverAll) {
    return FailoverAll(instances, unhealthy_set, result);
  } else if (failover_type == kMetadataFailoverNotKey) {
    return FailoverNotKey(instances, unhealthy_set, metadata, result);
  }
  return false;
}

bool MetadataServiceRouter::FailoverAll(const std::vector<Instance*>& instances,
                                        const std::set<Instance*>& unhealthy_set,
                                        std::vector<Instance*>& result) {
  std::vector<Instance*> unhealthy;
  for (std::size_t i = 0; i < instances.size(); ++i) {
    Instance* const& instance = instances[i];
    if (unhealthy_set.count(instance) == 0) {
      result.push_back(instance);
    } else {
      unhealthy.push_back(instance);
    }
  }
  if (!result.empty()) {
    return false;
  }
  if (!unhealthy.empty()) {
    result.swap(unhealthy);
    return true;
  }
  return false;
}

static bool MetadataMatchNotKey(const std::map<std::string, std::string>& metadata,
                                const std::map<std::string, std::string>& instance_metadata) {
  if (instance_metadata.empty()) {
    return true;
  }
  std::map<std::string, std::string>::const_iterator it;
  for (it = metadata.begin(); it != metadata.end(); ++it) {
    if (instance_metadata.count(it->first) != 0) {
      return false;
    }
  }
  return true;
}

bool MetadataServiceRouter::FailoverNotKey(const std::vector<Instance*>& instances,
                                           const std::set<Instance*>& unhealthy_set,
                                           const std::map<std::string, std::string>& metadata,
                                           std::vector<Instance*>& result) {
  std::vector<Instance*> unhealthy;
  for (std::size_t i = 0; i < instances.size(); ++i) {
    Instance* const& instance = instances[i];
    if (MetadataMatchNotKey(metadata, instance->GetMetadata())) {
      if (unhealthy_set.count(instance) == 0) {
        result.push_back(instance);
      } else {
        unhealthy.push_back(instance);
      }
    }
  }
  if (!result.empty()) {
    return false;
  }
  if (!unhealthy.empty()) {
    result.swap(unhealthy);
    return true;
  }
  return false;
}

ReturnCode MetadataServiceRouter::DoRoute(RouteInfo& route_info, RouteResult* route_result) {
  POLARIS_CHECK_ARGUMENT(route_result != NULL);

  ServiceInstances* service_instances = route_info.GetServiceInstances();
  POLARIS_CHECK_ARGUMENT(service_instances != NULL);
  ContextImpl* context_impl     = context_->GetContextImpl();
  const ServiceKey& service_key = service_instances->GetServiceData()->GetServiceKey();

  // 优先查询缓存
  MetadataCacheKey cache_key;
  cache_key.prior_data_ = service_instances->GetAvailableInstances();
  cache_key.circuit_breaker_version_ =
      service_instances->GetService()->GetCircuitBreakerDataVersion();
  cache_key.failover_type_ = kMetadataFailoverNone;
  if (!route_info.GetMetadata().empty()) {
    cache_key.metadata_      = route_info.GetMetadata();
    cache_key.failover_type_ = route_info.GetMetadataFailoverType();
  }

  CacheValueBase* cache_value_base = router_cache_->GetWithRef(cache_key);
  RouterSubsetCache* cache_value   = NULL;
  if (cache_value_base != NULL) {
    cache_value = dynamic_cast<RouterSubsetCache*>(cache_value_base);
    POLARIS_ASSERT(cache_value != NULL);
  } else {
    InstancesSet* prior_result = cache_key.prior_data_;
    POLARIS_ASSERT(prior_result != NULL);
    std::set<Instance*> unhealthy_set;
    CalculateUnhealthySet(route_info, service_instances, unhealthy_set);

    std::vector<Instance*> result;
    bool recover_all = CalculateResult(prior_result->GetInstances(), unhealthy_set,
                                       cache_key.metadata_, cache_key.failover_type_, result);

    cache_value                  = new RouterSubsetCache();
    cache_value->instances_data_ = service_instances->GetServiceData();
    cache_value->instances_data_->IncrementRef();
    cache_value->current_data_ = new InstancesSet(result, cache_key.metadata_);
    router_cache_->PutWithRef(cache_key, cache_value);
    if (recover_all) {
      if (!prior_result->recover_all_ && prior_result->recover_all_.Cas(false, true)) {
        context_impl->GetServiceRecord()->InstanceRecoverAll(
            service_key, new RecoverAllRecord(Time::GetCurrentTimeMs(), "metadata router", true));
      }
    } else {
      if (prior_result->recover_all_ && prior_result->recover_all_.Cas(true, false)) {
        context_impl->GetServiceRecord()->InstanceRecoverAll(
            service_key, new RecoverAllRecord(Time::GetCurrentTimeMs(), "metadata router", false));
      }
    }
  }
  if (!route_info.GetMetadata().empty()) {
    cache_value->current_data_->count_++;
  }
  service_instances->UpdateAvailableInstances(cache_value->current_data_);
  cache_value->DecrementRef();
  route_result->SetServiceInstances(service_instances);
  route_info.SetServiceInstances(NULL);
  return kReturnOk;
}

RouterStatData* MetadataServiceRouter::CollectStat() { return router_cache_->CollectStat(); }

}  // namespace polaris
