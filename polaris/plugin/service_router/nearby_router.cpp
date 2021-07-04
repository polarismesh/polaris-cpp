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

#include "nearby_router.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "cache/service_cache.h"
#include "context_internal.h"
#include "logger.h"
#include "model/location.h"
#include "model/model_impl.h"
#include "monitor/service_record.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "polaris/model.h"
#include "service_router.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

NearbyRouterConfig::NearbyRouterConfig()
    : match_level_(kNearbyMatchNone), max_match_level_(kNearbyMatchNone), strict_nearby_(false),
      enable_degrade_by_unhealthy_percent_(true), unhealthy_percent_to_degrade_(100),
      enable_recover_all_(true) {}

bool NearbyRouterConfig::Init(Config* config) {
  return InitNearbyMatchLevel(config) && InitStrictNearby(config) && InitDegradeConfig(config) &&
         InitRecoverConfig(config);
}

static const char kMatchLevelRegion[] = "region";
static const char kMatchLevelZone[]   = "zone";  // 如果未配置则默认zone级别
static const char kMatchLevelCampus[] = "campus";
static const char kMatchLevelNone[]   = "none";  // 默认最大就近级别

bool NearbyRouterConfig::StrToMatchLevel(const std::string& str, NearbyMatchLevel& match_level) {
  if (str == kMatchLevelRegion) {
    match_level = kNearbyMatchRegion;
  } else if (str == kMatchLevelZone) {
    match_level = kNearbyMatchZone;
  } else if (str == kMatchLevelCampus) {
    match_level = kNearbyMatchCampus;
  } else if (str == kMatchLevelNone) {
    match_level = kNearbyMatchNone;
  } else {
    return false;
  }
  return true;
}

bool NearbyRouterConfig::InitNearbyMatchLevel(Config* config) {
  // 就近级别配置key和可配置的value
  static const char kMatchLevelKey[]    = "matchLevel";
  static const char kMaxMatchLevelKey[] = "maxMatchLevel";

  std::string match_level_str = config->GetStringOrDefault(kMatchLevelKey, kMatchLevelZone);
  if (!StrToMatchLevel(match_level_str, match_level_)) {
    POLARIS_LOG(LOG_ERROR, "%s must be one of [%s, %s, %s, %s], value[%s] is invalid",
                kMatchLevelKey, kMatchLevelRegion, kMatchLevelZone, kMatchLevelCampus,
                kMatchLevelNone, match_level_str.c_str());
    return false;
  }
  std::string max_match_level_str = config->GetStringOrDefault(kMaxMatchLevelKey, kMatchLevelNone);
  if (!StrToMatchLevel(max_match_level_str, max_match_level_)) {
    POLARIS_LOG(LOG_ERROR, "%s must be one of [%s, %s, %s, %s], value[%s] is invalid",
                kMaxMatchLevelKey, kMatchLevelRegion, kMatchLevelZone, kMatchLevelCampus,
                kMatchLevelNone, max_match_level_str.c_str());
    return false;
  }
  if (match_level_ < max_match_level_) {
    POLARIS_LOG(LOG_ERROR, "%s[%s] higher than %s[%s], this is invalid", kMatchLevelKey,
                match_level_str.c_str(), kMaxMatchLevelKey, max_match_level_str.c_str());
    return false;
  }
  return true;
}

bool NearbyRouterConfig::InitStrictNearby(Config* config) {
  // 严格就近配置key和默认值，配置是否严格就近
  // 如果为true，则必须从服务器获取到主调的位置信息后才能执行就近，并且在就近级别没有健康实例时不会进行降级
  // 如果为false，则在未获取到主调位置信息时也可进行就近路由，在就近未找到健康实例时会降级查找
  static const char kStrictNearbyKey[]   = "strictNearby";
  static const bool kStrictNearbyDefault = false;

  strict_nearby_ = config->GetBoolOrDefault(kStrictNearbyKey, kStrictNearbyDefault);
  return true;
}

bool NearbyRouterConfig::InitDegradeConfig(Config* config) {
  // 降级配置key和默认value
  static const char kEnableDegradeByUnhealthyPercentKey[]   = "enableDegradeByUnhealthyPercent";
  static const bool kEnableDegradeByUnhealthyPercentDefault = true;
  static const char kUnhealthyPercentToDegradeKey[]         = "unhealthyPercentToDegrade";
  static const float kUnhealthyPercentToDegradeDefault      = 100;

  enable_degrade_by_unhealthy_percent_ = config->GetBoolOrDefault(
      kEnableDegradeByUnhealthyPercentKey, kEnableDegradeByUnhealthyPercentDefault);

  unhealthy_percent_to_degrade_ =
      config->GetIntOrDefault(kUnhealthyPercentToDegradeKey, kUnhealthyPercentToDegradeDefault);
  if (unhealthy_percent_to_degrade_ <= 0 || unhealthy_percent_to_degrade_ > 100) {
    POLARIS_LOG(LOG_ERROR, "%s must be in (0, 100] , config value[%d] is invalid",
                kUnhealthyPercentToDegradeKey, unhealthy_percent_to_degrade_);
    return false;
  }
  return true;
}

bool NearbyRouterConfig::InitRecoverConfig(Config* config) {
  // 全死全活开配置key和默认value
  static const char kRecoverAllEnableKey[]   = "enableRecoverAll";
  static const bool kRecoverAllEnableDefault = true;

  enable_recover_all_ = config->GetBoolOrDefault(kRecoverAllEnableKey, kRecoverAllEnableDefault);
  return true;
}

///////////////////////////////////////////////////////////////////////////////

NearbyRouterCluster::NearbyRouterCluster(const NearbyRouterConfig& nearby_router_config)
    : config_(nearby_router_config) {
  data_.resize(config_.GetMatchLevel() + 1);
}

void NearbyRouterCluster::CalculateSet(const Location& location,
                                       const std::vector<Instance*>& instances,
                                       const std::set<Instance*>& unhealthy_set) {
  for (std::size_t i = 0; i < instances.size(); ++i) {
    Instance* const& instance = instances[i];
    uint32_t level            = 0;
    if (config_.GetMatchLevel() >= kNearbyMatchRegion && location.region == instance->GetRegion()) {
      ++level;
      if (config_.GetMatchLevel() >= kNearbyMatchZone && location.zone == instance->GetZone()) {
        ++level;
        if (config_.GetMatchLevel() >= kNearbyMatchCampus &&
            location.campus == instance->GetCampus()) {
          ++level;
        }
      }
    }
    if (unhealthy_set.find(instance) == unhealthy_set.end()) {
      data_[level].healthy_.push_back(instance);
    } else {
      data_[level].unhealthy_.push_back(instance);
    }
  }
}

void NearbyRouterCluster::CalculateSet(const std::vector<Instance*>& instances,
                                       const std::set<Instance*>& unhealthy_set) {
  uint32_t level = config_.GetMatchLevel();
  for (std::size_t i = 0; i < instances.size(); ++i) {
    Instance* const& instance = instances[i];
    if (unhealthy_set.find(instance) == unhealthy_set.end()) {
      data_[level].healthy_.push_back(instance);
    } else {
      data_[level].unhealthy_.push_back(instance);
    }
  }
}

bool NearbyRouterCluster::CalculateResult(std::vector<Instance*>& result, int& match_level) {
  match_level = config_.GetMatchLevel();
  std::size_t total_size =
      data_[match_level].healthy_.size() + data_[match_level].unhealthy_.size();
  while (total_size <= 0 && match_level > config_.GetMaxMatchLevel()) {
    --match_level;
    total_size = data_[match_level].healthy_.size() + data_[match_level].unhealthy_.size();
  }
  if (total_size <= 0) {  // 可选择的就近级别内都没有匹配到实例，直接返回
    POLARIS_LOG(LOG_DEBUG, "no instances available in match level[%d, %d]", config_.GetMatchLevel(),
                config_.GetMaxMatchLevel());
    return false;
  }

  std::size_t unhealthy_size = data_[match_level].unhealthy_.size();
  // 判断是否需要降级，并检查不健康实例比例
  if (config_.IsEnableDegradeByUnhealthyPercent() &&
      unhealthy_size * 100 >= total_size * config_.GetUnhealthyPercentToDegrade()) {
    // 降级加入依次加入其它就近级别实例，直到满足条件为止
    int degrade_to_level = -1;
    for (int level = match_level - 1; level >= config_.GetMaxMatchLevel(); --level) {
      total_size += (data_[level].healthy_.size() + data_[level].unhealthy_.size());
      unhealthy_size += data_[level].unhealthy_.size();
      if (unhealthy_size * 100 < total_size * config_.GetUnhealthyPercentToDegrade()) {
        degrade_to_level = level;  // 降级成功
        break;
      }
    }
    if (degrade_to_level >= config_.GetMaxMatchLevel()) {
      result.reserve(total_size - unhealthy_size);
      for (int level = match_level; level >= degrade_to_level; --level) {
        result.insert(result.end(), data_[level].healthy_.begin(), data_[level].healthy_.end());
      }
      return true;  // 降级成功直接返回
    }
  }

  // 不允许降级、无需降级或降级失败，返回匹配到的最近的分组
  if (!data_[match_level].healthy_.empty()) {
    result.swap(data_[match_level].healthy_);
  } else if (config_.IsEnableRecoverAll()) {
    result.swap(data_[match_level].unhealthy_);
    return true;
  }
  return match_level != config_.GetMatchLevel();
}

NearbyServiceRouter::NearbyServiceRouter() : context_(NULL), router_cache_(NULL) {}

NearbyServiceRouter::~NearbyServiceRouter() {
  context_ = NULL;
  if (router_cache_ != NULL) {
    router_cache_->SetClearHandler(0);
    router_cache_->DecrementRef();
    router_cache_ = NULL;
  }
}

ReturnCode NearbyServiceRouter::Init(Config* config, Context* context) {
  if (!nearby_router_config_.Init(config)) {
    return kReturnInvalidConfig;
  }
  context_ = context;
  // 严格就近时检查位置信息
  if (nearby_router_config_.IsStrictNearby() && !CheckLocation()) {
    POLARIS_LOG(LOG_FATAL, "nearby router config strict is true, but get client location error");
    return kReturnInvalidConfig;
  }
  router_cache_ = new ServiceCache<NearbyCacheKey>();
  context->GetContextImpl()->RegisterCache(router_cache_);
  return kReturnOk;
}

bool NearbyServiceRouter::CheckLocation() {
  ContextImpl* context_impl = context_->GetContextImpl();
  model::VersionedLocation versioned_location;
  context_impl->GetClientLocation().GetVersionedLocation(versioned_location);
  Location& location = versioned_location.location_;
  if (nearby_router_config_.GetMatchLevel() > kNearbyMatchNone && location.region.empty()) {
    return false;
  }
  if (nearby_router_config_.GetMatchLevel() > kNearbyMatchRegion && location.zone.empty()) {
    return false;
  }
  if (nearby_router_config_.GetMatchLevel() > kNearbyMatchZone && location.campus.empty()) {
    return false;
  }
  return true;
}

void NearbyServiceRouter::GetLocationByMatchLevel(const Location& location, int match_level,
                                                  std::string& level_key,
                                                  std::string& level_value) {
  switch (match_level) {
    case kNearbyMatchRegion:
      level_key   = kMatchLevelRegion;
      level_value = location.region;
      break;
    case kNearbyMatchZone:
      level_key   = kMatchLevelZone;
      level_value = location.zone;
      break;
    case kNearbyMatchCampus:
      level_key   = kMatchLevelCampus;
      level_value = location.campus;
      break;
    default:
      level_key = kMatchLevelNone;
      level_value.clear();
      break;
  }
}

ReturnCode NearbyServiceRouter::DoRoute(RouteInfo& route_info, RouteResult* route_result) {
  POLARIS_CHECK_ARGUMENT(route_result != NULL);

  ServiceInstances* service_instances = route_info.GetServiceInstances();
  POLARIS_CHECK_ARGUMENT(service_instances != NULL);
  ContextImpl* context_impl     = context_->GetContextImpl();
  const ServiceKey& service_key = service_instances->GetServiceData()->GetServiceKey();

  // 优先查询缓存
  NearbyCacheKey cache_key;
  cache_key.prior_data_ = service_instances->GetAvailableInstances();
  cache_key.circuit_breaker_version_ =
      service_instances->GetService()->GetCircuitBreakerDataVersion();
  cache_key.request_flags_    = route_info.GetRequestFlags();
  cache_key.location_version_ = 0;  // 默认不开启就近，位置信息版本为0
  // 查询服务是否通过元数据配置开启就近路由
  if (service_instances->IsNearbyEnable()) {
    cache_key.location_version_ = context_impl->GetClientLocation().GetVersion();
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
    NearbyRouterCluster nearby_cluster(nearby_router_config_);
    model::VersionedLocation location;
    context_impl->GetClientLocation().GetVersionedLocation(location);
    if (cache_key.location_version_ == 0) {
      location.version_ = 0;  // 就近未开启，更新version用于发生全死全活的时候上报
      nearby_cluster.CalculateSet(prior_result->GetInstances(), unhealthy_set);
    } else {
      cache_key.location_version_ = location.version_;  // 更新key中的version
      nearby_cluster.CalculateSet(location.location_, prior_result->GetInstances(), unhealthy_set);
    }
    std::vector<Instance*> result;
    int match_level;
    bool recover_all             = nearby_cluster.CalculateResult(result, match_level);
    cache_value                  = new RouterSubsetCache();
    cache_value->instances_data_ = service_instances->GetServiceData();
    cache_value->instances_data_->IncrementRef();
    std::map<std::string, std::string> subset;
    std::string match_level_key, match_level_value;
    GetLocationByMatchLevel(location.location_, match_level, match_level_key, match_level_value);
    subset[match_level_key] = match_level_value;
    if (recover_all) {
      GetLocationByMatchLevel(location.location_, nearby_router_config_.GetMatchLevel(),
                              match_level_key, match_level_value);
      cache_value->current_data_ =
          new InstancesSet(result, subset, "from " + match_level_key + ":" + match_level_value);
      if (!prior_result->recover_all_ && prior_result->recover_all_.Cas(false, true)) {
        context_impl->GetServiceRecord()->InstanceRecoverAll(
            service_key, new RecoverAllRecord(Time::GetCurrentTimeMs(), location.ToString(), true));
      }
    } else {
      cache_value->current_data_ = new InstancesSet(result, subset);
      if (prior_result->recover_all_ && prior_result->recover_all_.Cas(true, false)) {
        context_impl->GetServiceRecord()->InstanceRecoverAll(
            service_key,
            new RecoverAllRecord(Time::GetCurrentTimeMs(), location.ToString(), false));
      }
    }
    router_cache_->PutWithRef(cache_key, cache_value);
  }
  if (service_instances->IsNearbyEnable()) {
    cache_value->current_data_->count_++;
  }
  service_instances->UpdateAvailableInstances(cache_value->current_data_);
  cache_value->DecrementRef();
  route_result->SetServiceInstances(service_instances);
  route_info.SetServiceInstances(NULL);
  return kReturnOk;
}

RouterStatData* NearbyServiceRouter::CollectStat() { return router_cache_->CollectStat(); }

}  // namespace polaris
