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
#include "context/context_impl.h"
#include "logger.h"
#include "model/constants.h"
#include "model/location.h"
#include "model/model_impl.h"
#include "monitor/service_record.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "polaris/model.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

NearbyRouterConfig::NearbyRouterConfig()
    : match_level_(kNearbyMatchNone),
      max_match_level_(kNearbyMatchNone),
      strict_nearby_(false),
      enable_degrade_by_unhealthy_percent_(true),
      unhealthy_percent_to_degrade_(100),
      enable_recover_all_(true) {}

bool NearbyRouterConfig::Init(Config* config) {
  return InitNearbyMatchLevel(config) && InitStrictNearby(config) && InitDegradeConfig(config) &&
         InitRecoverConfig(config);
}

bool NearbyRouterConfig::StrToMatchLevel(const std::string& str, NearbyMatchLevel& match_level) {
  if (str == constants::kLocationRegion) {
    match_level = kNearbyMatchRegion;
  } else if (str == constants::kLocationZone) {
    match_level = kNearbyMatchZone;
  } else if (str == constants::kLocationCampus) {
    match_level = kNearbyMatchCampus;
  } else if (str == constants::kLocationNone) {
    match_level = kNearbyMatchNone;
  } else {
    return false;
  }
  return true;
}

bool NearbyRouterConfig::InitNearbyMatchLevel(Config* config) {
  // ??????????????????key???????????????value
  static const char kMatchLevelKey[] = "matchLevel";
  static const char kMaxMatchLevelKey[] = "maxMatchLevel";

  std::string match_level_str = config->GetStringOrDefault(kMatchLevelKey, constants::kLocationZone);
  if (!StrToMatchLevel(match_level_str, match_level_)) {
    POLARIS_LOG(LOG_ERROR, "%s must be one of [%s, %s, %s, %s], value[%s] is invalid", kMatchLevelKey,
                constants::kLocationRegion, constants::kLocationZone, constants::kLocationCampus,
                constants::kLocationNone, match_level_str.c_str());
    return false;
  }
  std::string max_match_level_str = config->GetStringOrDefault(kMaxMatchLevelKey, constants::kLocationNone);
  if (!StrToMatchLevel(max_match_level_str, max_match_level_)) {
    POLARIS_LOG(LOG_ERROR, "%s must be one of [%s, %s, %s, %s], value[%s] is invalid", kMaxMatchLevelKey,
                constants::kLocationRegion, constants::kLocationZone, constants::kLocationCampus,
                constants::kLocationNone, max_match_level_str.c_str());
    return false;
  }
  if (match_level_ < max_match_level_) {
    POLARIS_LOG(LOG_ERROR, "%s[%s] higher than %s[%s], this is invalid", kMatchLevelKey, match_level_str.c_str(),
                kMaxMatchLevelKey, max_match_level_str.c_str());
    return false;
  }
  return true;
}

bool NearbyRouterConfig::InitStrictNearby(Config* config) {
  // ??????????????????key???????????????????????????????????????
  // ?????????true???????????????????????????????????????????????????????????????????????????
  // ?????????false??????????????????????????????????????????????????????????????????
  static const char kStrictNearbyKey[] = "strictNearby";
  static const bool kStrictNearbyDefault = false;

  strict_nearby_ = config->GetBoolOrDefault(kStrictNearbyKey, kStrictNearbyDefault);
  return true;
}

bool NearbyRouterConfig::InitDegradeConfig(Config* config) {
  // ????????????key?????????value
  static const char kEnableDegradeByUnhealthyPercentKey[] = "enableDegradeByUnhealthyPercent";
  static const bool kEnableDegradeByUnhealthyPercentDefault = true;
  static const char kUnhealthyPercentToDegradeKey[] = "unhealthyPercentToDegrade";
  static const int kUnhealthyPercentToDegradeDefault = 100;

  enable_degrade_by_unhealthy_percent_ =
      config->GetBoolOrDefault(kEnableDegradeByUnhealthyPercentKey, kEnableDegradeByUnhealthyPercentDefault);

  unhealthy_percent_to_degrade_ =
      config->GetIntOrDefault(kUnhealthyPercentToDegradeKey, kUnhealthyPercentToDegradeDefault);
  if (unhealthy_percent_to_degrade_ <= 0 || unhealthy_percent_to_degrade_ > 100) {
    POLARIS_LOG(LOG_ERROR, "%s must be in (0, 100] , config value[%d] is invalid", kUnhealthyPercentToDegradeKey,
                unhealthy_percent_to_degrade_);
    return false;
  }
  return true;
}

bool NearbyRouterConfig::InitRecoverConfig(Config* config) {
  // ?????????????????????key?????????value
  static const char kRecoverAllEnableKey[] = "enableRecoverAll";
  static const bool kRecoverAllEnableDefault = true;

  enable_recover_all_ = config->GetBoolOrDefault(kRecoverAllEnableKey, kRecoverAllEnableDefault);
  return true;
}

///////////////////////////////////////////////////////////////////////////////

NearbyRouterCluster::NearbyRouterCluster(const NearbyRouterConfig& nearby_router_config)
    : config_(nearby_router_config) {
  data_.resize(config_.GetMatchLevel() + 1);
}

void NearbyRouterCluster::CalculateSet(const Location& location, const std::vector<Instance*>& instances,
                                       const std::set<Instance*>& unhealthy_set) {
  for (std::size_t i = 0; i < instances.size(); ++i) {
    Instance* const& instance = instances[i];
    uint32_t level = 0;
    if (config_.GetMatchLevel() >= kNearbyMatchRegion && location.region == instance->GetRegion()) {
      ++level;
      if (config_.GetMatchLevel() >= kNearbyMatchZone && location.zone == instance->GetZone()) {
        ++level;
        if (config_.GetMatchLevel() >= kNearbyMatchCampus && location.campus == instance->GetCampus()) {
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
  std::size_t total_size = data_[match_level].healthy_.size() + data_[match_level].unhealthy_.size();
  while (total_size <= 0 && match_level > config_.GetMaxMatchLevel()) {
    --match_level;
    total_size = data_[match_level].healthy_.size() + data_[match_level].unhealthy_.size();
  }
  if (total_size <= 0) {  // ??????????????????????????????????????????????????????????????????
    POLARIS_LOG(LOG_DEBUG, "no instances available in match level[%d, %d]", config_.GetMatchLevel(),
                config_.GetMaxMatchLevel());
    return false;
  }

  std::size_t unhealthy_size = data_[match_level].unhealthy_.size();
  // ?????????????????????????????????????????????????????????
  if (config_.IsEnableDegradeByUnhealthyPercent() &&
      unhealthy_size * 100 >= total_size * config_.GetUnhealthyPercentToDegrade()) {
    // ???????????????????????????????????????????????????????????????????????????
    int degrade_to_level = -1;
    for (int level = match_level - 1; level >= config_.GetMaxMatchLevel(); --level) {
      total_size += (data_[level].healthy_.size() + data_[level].unhealthy_.size());
      unhealthy_size += data_[level].unhealthy_.size();
      if (unhealthy_size * 100 < total_size * config_.GetUnhealthyPercentToDegrade()) {
        degrade_to_level = level;  // ????????????
        break;
      }
    }
    if (degrade_to_level >= config_.GetMaxMatchLevel()) {
      result.reserve(total_size - unhealthy_size);
      for (int level = match_level; level >= degrade_to_level; --level) {
        result.insert(result.end(), data_[level].healthy_.begin(), data_[level].healthy_.end());
      }
      return true;  // ????????????????????????
    }
  }

  // ?????????????????????????????????????????????????????????????????????????????????
  if (!data_[match_level].healthy_.empty()) {
    result.swap(data_[match_level].healthy_);
  } else if (config_.IsEnableRecoverAll()) {
    result.swap(data_[match_level].unhealthy_);
    return true;
  }
  return match_level != config_.GetMatchLevel();
}

NearbyServiceRouter::NearbyServiceRouter() : context_(nullptr), router_cache_(nullptr) {}

NearbyServiceRouter::~NearbyServiceRouter() {
  context_ = nullptr;
  if (router_cache_ != nullptr) {
    router_cache_->SetClearHandler(0);
    router_cache_->DecrementRef();
    router_cache_ = nullptr;
  }
}

ReturnCode NearbyServiceRouter::Init(Config* config, Context* context) {
  if (!nearby_router_config_.Init(config)) {
    return kReturnInvalidConfig;
  }
  context_ = context;
  // ?????????????????????????????????
  if (nearby_router_config_.IsStrictNearby() && !CheckLocation()) {
    POLARIS_LOG(LOG_FATAL, "nearby router config strict is true, but get client location error");
    return kReturnInvalidConfig;
  }
  router_cache_ = new ServiceCache<NearbyCacheKey, RouterSubsetCache>();
  context->GetContextImpl()->RegisterCache(router_cache_);
  return kReturnOk;
}

bool NearbyServiceRouter::CheckLocation() {
  ContextImpl* context_impl = context_->GetContextImpl();
  Location location;
  context_impl->GetClientLocation().GetLocation(location);
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

void NearbyServiceRouter::GetLocationByMatchLevel(const Location& location, int match_level, std::string& level_key,
                                                  std::string& level_value) {
  switch (match_level) {
    case kNearbyMatchRegion:
      level_key = constants::kLocationRegion;
      level_value = location.region;
      break;
    case kNearbyMatchZone:
      level_key = constants::kLocationZone;
      level_value = location.zone;
      break;
    case kNearbyMatchCampus:
      level_key = constants::kLocationCampus;
      level_value = location.campus;
      break;
    default:
      level_key = constants::kLocationNone;
      level_value.clear();
      break;
  }
}

ReturnCode NearbyServiceRouter::DoRoute(RouteInfo& route_info, RouteResult* route_result) {
  if (route_info.IsNearbyRouterDisable()) {
    return kReturnOk;
  }

  // ??????????????????
  NearbyCacheKey cache_key;
  ServiceInstances* service_instances = route_info.GetServiceInstances();
  cache_key.prior_data_ = service_instances->GetAvailableInstances();
  cache_key.circuit_breaker_version_ = route_info.GetCircuitBreakerVersion();
  cache_key.request_flags_ = route_info.GetRequestFlags();
  cache_key.location_version_ = 0;  // ?????????????????????????????????????????????0
  // ?????????????????????????????????????????????????????????
  ContextImpl* context_impl = context_->GetContextImpl();
  if (service_instances->IsNearbyEnable()) {
    cache_key.location_version_ = context_impl->GetClientLocation().GetVersion();
  }

  RouterSubsetCache* cache_value = router_cache_->GetWithRcuTime(cache_key);
  if (cache_value == nullptr) {
    cache_value = router_cache_->CreateOrGet(cache_key, [&] {
      InstancesSet* prior_result = cache_key.prior_data_;
      std::set<Instance*> unhealthy_set;
      route_info.CalculateUnhealthySet(unhealthy_set);
      NearbyRouterCluster nearby_cluster(nearby_router_config_);
      Location location;
      uint32_t location_version;
      context_impl->GetClientLocation().GetLocation(location, location_version);
      if (service_instances->IsNearbyEnable()) {
        cache_key.location_version_ = location_version;  // ??????key??????version
        nearby_cluster.CalculateSet(location, prior_result->GetInstances(), unhealthy_set);
      } else {
        location_version = 0;  // ????????????????????????version???????????????????????????????????????
        nearby_cluster.CalculateSet(prior_result->GetInstances(), unhealthy_set);
      }
      std::vector<Instance*> result;
      int match_level;
      bool recover_all = nearby_cluster.CalculateResult(result, match_level);
      RouterSubsetCache* new_cache_value = new RouterSubsetCache();
      new_cache_value->instances_data_ = service_instances->GetServiceData();
      new_cache_value->instances_data_->IncrementRef();
      std::map<std::string, std::string> subset;
      std::string match_level_key, match_level_value;
      GetLocationByMatchLevel(location, match_level, match_level_key, match_level_value);
      subset[match_level_key] = match_level_value;
      if (recover_all) {
        GetLocationByMatchLevel(location, nearby_router_config_.GetMatchLevel(), match_level_key, match_level_value);
        new_cache_value->current_data_ =
            new InstancesSet(result, subset, "from " + match_level_key + ":" + match_level_value);
      } else {
        new_cache_value->current_data_ = new InstancesSet(result, subset);
      }
      if (prior_result->GetImpl()->UpdateRecoverAll(recover_all)) {
        const ServiceKey& service_key = service_instances->GetServiceData()->GetServiceKey();
        std::string location_string = ClientLocation::ToString(location, location_version);
        context_impl->GetServiceRecord()->InstanceRecoverAll(
            service_key, new RecoverAllRecord(Time::GetSystemTimeMs(), location_string, recover_all));
      }
      route_result->SetNewInstancesSet();
      return new_cache_value;
    });
  }
  if (service_instances->IsNearbyEnable()) {
    cache_value->current_data_->GetImpl()->count_++;
  }
  service_instances->UpdateAvailableInstances(cache_value->current_data_);
  return kReturnOk;
}

RouterStatData* NearbyServiceRouter::CollectStat() { return router_cache_->CollectStat(); }

}  // namespace polaris
