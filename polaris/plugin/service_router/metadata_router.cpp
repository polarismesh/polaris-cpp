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
#include "context/context_impl.h"
#include "logger.h"
#include "model/model_impl.h"
#include "monitor/service_record.h"
#include "polaris/context.h"
#include "polaris/model.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

class Config;

MetadataServiceRouter::MetadataServiceRouter() : context_(nullptr), router_cache_(nullptr) {}

MetadataServiceRouter::~MetadataServiceRouter() {
  context_ = nullptr;
  if (router_cache_ != nullptr) {
    router_cache_->SetClearHandler(0);
    router_cache_->DecrementRef();
    router_cache_ = nullptr;
  }
}

ReturnCode MetadataServiceRouter::Init(Config* /*config*/, Context* context) {
  context_ = context;
  router_cache_ = new ServiceCache<MetadataCacheKey, RouterSubsetCache>();
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
                                            MetadataFailoverType failover_type, std::vector<Instance*>& result) {
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
                                        const std::set<Instance*>& unhealthy_set, std::vector<Instance*>& result) {
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
  // 优先查询缓存
  MetadataCacheKey cache_key;
  ServiceInstances* service_instances = route_info.GetServiceInstances();
  cache_key.prior_data_ = service_instances->GetAvailableInstances();
  cache_key.circuit_breaker_version_ = route_info.GetCircuitBreakerVersion();
  cache_key.failover_type_ = kMetadataFailoverNone;
  if (!route_info.GetMetadata().empty()) {
    cache_key.metadata_ = route_info.GetMetadata();
    cache_key.failover_type_ = route_info.GetMetadataFailoverType();
  }

  RouterSubsetCache* cache_value = router_cache_->GetWithRcuTime(cache_key);
  if (cache_value == nullptr) {
    cache_value = router_cache_->CreateOrGet(cache_key, [&] {
      InstancesSet* prior_result = cache_key.prior_data_;
      std::set<Instance*> unhealthy_set;
      route_info.CalculateUnhealthySet(unhealthy_set);
      std::vector<Instance*> result;
      bool recover_all = CalculateResult(prior_result->GetInstances(), unhealthy_set, cache_key.metadata_,
                                         cache_key.failover_type_, result);

      RouterSubsetCache* new_cache_value = new RouterSubsetCache();
      new_cache_value->instances_data_ = service_instances->GetServiceData();
      new_cache_value->instances_data_->IncrementRef();
      new_cache_value->current_data_ = new InstancesSet(result, cache_key.metadata_);
      route_result->SetNewInstancesSet();
      if (prior_result->GetImpl()->UpdateRecoverAll(recover_all)) {
        const ServiceKey& service_key = service_instances->GetServiceData()->GetServiceKey();
        context_->GetContextImpl()->GetServiceRecord()->InstanceRecoverAll(
            service_key, new RecoverAllRecord(Time::GetSystemTimeMs(), "metadata router", recover_all));
      }
      return new_cache_value;
    });
  }
  if (!route_info.GetMetadata().empty()) {
    cache_value->current_data_->GetImpl()->count_++;
  }
  service_instances->UpdateAvailableInstances(cache_value->current_data_);
  return kReturnOk;
}

RouterStatData* MetadataServiceRouter::CollectStat() { return router_cache_->CollectStat(); }

}  // namespace polaris
