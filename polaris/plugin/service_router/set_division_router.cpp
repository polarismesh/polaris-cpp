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

#include "set_division_router.h"

#include <string.h>

#include <iosfwd>
#include <utility>

#include "cache/service_cache.h"
#include "context_internal.h"
#include "logger.h"
#include "model/constants.h"
#include "model/model_impl.h"
#include "polaris/context.h"
#include "polaris/model.h"
#include "service_router.h"

namespace polaris {

class Config;

const char SetDivisionServiceRouter::enable_set_key[]   = "internal-enable-set";
const char SetDivisionServiceRouter::enable_set_force[] = "enable-set-force";

SetDivisionServiceRouter::SetDivisionServiceRouter() : context_(NULL), router_cache_(NULL) {}

SetDivisionServiceRouter::~SetDivisionServiceRouter() {
  context_ = NULL;
  if (router_cache_ != NULL) {
    router_cache_->SetClearHandler(0);
    router_cache_->DecrementRef();
    router_cache_ = NULL;
  }
}

ReturnCode SetDivisionServiceRouter::Init(Config* /*config*/, Context* context) {
  context_      = context;
  router_cache_ = new ServiceCache<SetDivisionCacheKey>();
  context->GetContextImpl()->RegisterCache(router_cache_);
  return kReturnOk;
}

// 根据主调和被调的metadata判断是否启用set分组
bool SetDivisionServiceRouter::IsSetDivisionRouterEnable(
    const std::string& caller_set_name, const std::string& callee_set_name,
    const std::map<std::string, std::string>& callee_metadata) {
  // 主调或被调未设置set名，则不启用set规则
  if (caller_set_name.empty() || callee_metadata.empty()) {
    return false;
  }

  // 校验主调的set名格式, 如果set名格式非法，则认为未启用set
  if (caller_set_name.find_first_of(".") == caller_set_name.find_last_of(".")) {
    POLARIS_LOG(LOG_ERROR, "Setname format invalid, caller_set_name = %s", caller_set_name.c_str());
    return false;
  }

  std::map<std::string, std::string>::const_iterator iter =
      callee_metadata.find(SetDivisionServiceRouter::enable_set_key);
  if (iter == callee_metadata.end() || strcasecmp(iter->second.c_str(), "Y")) {
    return false;
  }

  // 查看主调的set名第一段和被调的set名第一段是否匹配
  std::string caller_first_set_field =
      caller_set_name.substr(0, caller_set_name.find_first_of("."));
  std::string callee_first_set_field =
      callee_set_name.substr(0, callee_set_name.find_first_of("."));
  if (caller_first_set_field == callee_first_set_field) {
    return true;
  }

  return false;
}

int SetDivisionServiceRouter::GetResultWithSetName(std::string set_name,
                                                   const std::vector<Instance*>& src_instances,
                                                   std::vector<Instance*>& result, bool wild) {
  for (std::size_t i = 0; i < src_instances.size(); ++i) {
    std::map<std::string, std::string>& metadata      = src_instances[i]->GetMetadata();
    std::map<std::string, std::string>::iterator iter = metadata.find(enable_set_key);
    // 被调未启用set，则跳过
    if (iter == metadata.end()) {
      continue;
    } else if (strcasecmp("Y", iter->second.c_str())) {  // internal-enable-set != Y/y
      continue;
    }

    // 被调instance的setname
    std::string& callee_set_name = src_instances[i]->GetInternalSetName();
    if (callee_set_name.empty()) {
      continue;
    }
    if (wild) {
      // 如果主调set_group_id为通配符的话，走通配逻辑
      if (callee_set_name.compare(0, set_name.size(), set_name) == 0) {
        result.push_back(src_instances[i]);
      }
    } else {
      // 全匹配逻辑
      if (callee_set_name.compare(set_name) == 0) {
        result.push_back(src_instances[i]);
      }
    }
  }

  return 0;
}

int SetDivisionServiceRouter::CalculateMatchResult(std::string caller_set_name,
                                                   const std::vector<Instance*>& src_instances,
                                                   std::vector<Instance*>& result) {
  std::string::size_type first_pos = caller_set_name.find_first_of(".");
  std::string::size_type last_pos  = caller_set_name.find_last_of(".");
  if (first_pos == last_pos || last_pos == std::string::npos) {
    POLARIS_LOG(LOG_ERROR, "exception occur, setname format invalid:%s", caller_set_name.c_str());
    return -1;
  }
  std::string set_name     = caller_set_name.substr(0, first_pos);
  std::string set_area     = caller_set_name.substr(first_pos + 1, last_pos - first_pos - 1);
  std::string set_group_id = caller_set_name.substr(last_pos + 1);

  if (set_group_id == "*") {
    GetResultWithSetName(set_name + "." + set_area, src_instances, result, true);
  } else {
    GetResultWithSetName(caller_set_name, src_instances, result, false);
    if (result.size() == 0) {
      GetResultWithSetName(set_name + "." + set_area + ".*", src_instances, result, false);
    }
  }

  return 0;
}

ReturnCode SetDivisionServiceRouter::DoRoute(RouteInfo& route_info, RouteResult* route_result) {
  POLARIS_CHECK_ARGUMENT(route_result != NULL);

  ServiceInstances* service_instances = route_info.GetServiceInstances();
  POLARIS_CHECK_ARGUMENT(service_instances != NULL);

  if (NULL == route_info.GetSourceServiceInfo()) {
    route_result->SetServiceInstances(service_instances);
    route_info.SetServiceInstances(NULL);
    return kReturnOk;
  }

  // 主调没有指定set信息，则认为未启用set
  std::map<std::string, std::string>& source_metadata =
      route_info.GetSourceServiceInfo()->metadata_;
  std::map<std::string, std::string>::iterator source_meta_iter =
      source_metadata.find(constants::kRouterRequestSetNameKey);
  if (source_meta_iter == source_metadata.end() || source_meta_iter->second.empty()) {
    route_result->SetServiceInstances(service_instances);
    route_info.SetServiceInstances(NULL);
    return kReturnOk;
  }

  // 先从缓存中读取，未找到的话就创建缓存
  SetDivisionCacheKey cache_key;
  cache_key.prior_data_     = service_instances->GetAvailableInstances();
  cache_key.caller_set_name = source_meta_iter->second;
  cache_key.circuit_breaker_version_ =
      service_instances->GetService()->GetCircuitBreakerDataVersion();

  cache_key.request_flags_           = route_info.GetRequestFlags();
  CacheValueBase* cache_value_base   = router_cache_->GetWithRef(cache_key);
  SetDivisionCacheValue* cache_value = NULL;
  if (cache_value_base != NULL) {
    cache_value = dynamic_cast<SetDivisionCacheValue*>(cache_value_base);
    if (cache_value == NULL) {
      cache_value_base->DecrementRef();
      return kReturnInvalidState;
    }
  } else {
    // 未强制启用
    bool enable_set = false;
    //判断是否启用set逻辑
    InstancesSet* all_avail_instances           = service_instances->GetAvailableInstances();
    const std::vector<Instance*>& all_instances = all_avail_instances->GetInstances();
    for (std::size_t i = 0; i < all_instances.size(); ++i) {
      if (IsSetDivisionRouterEnable(cache_key.caller_set_name,
                                    all_instances[i]->GetInternalSetName(),
                                    all_instances[i]->GetMetadata())) {
        enable_set = true;
        break;
      }
    }

    cache_value                  = new SetDivisionCacheValue();
    cache_value->instances_data_ = service_instances->GetServiceData();
    cache_value->instances_data_->IncrementRef();
    cache_value->enable_set = enable_set;

    if (enable_set == true) {
      // 启用set，则按set逻辑走
      InstancesSet* avail_instances = service_instances->GetAvailableInstances();
      // 从available列表中选出符合条件的instance实例
      std::vector<Instance*> result;
      const std::vector<Instance*>& instances = avail_instances->GetInstances();
      CalculateMatchResult(cache_key.caller_set_name, instances, result);
      // 从选出的列表中进一步选出active的节点，如果没有active的节点，将返回inactive的
      std::set<Instance*> unhealthy_set;
      CalculateUnhealthySet(route_info, service_instances, unhealthy_set);

      std::vector<Instance*> healthy_result;
      GetHealthyInstances(result, unhealthy_set, healthy_result);
      std::map<std::string, std::string> subset;
      if (!healthy_result.empty()) {
        subset["taf.set"]          = cache_key.caller_set_name;
        cache_value->current_data_ = new InstancesSet(healthy_result, subset);
      } else {  // 所有节点都死了，则返回所有
        subset["taf.set"]          = "*";
        cache_value->current_data_ = new InstancesSet(result, subset, "no healthy node");
      }
    } else {
      // 如果判断为启用set的话，则instanceSet设置为空即可
      std::vector<Instance*> empty_result;
      cache_value->current_data_ = new InstancesSet(empty_result);
    }

    router_cache_->PutWithRef(cache_key, cache_value);
  }

  bool enable_set_force = false;
  // 先判断是否有设置强制启用set路由规则，如果未设置的话则使用set规则判断是否启用set
  source_meta_iter = source_metadata.find(SetDivisionServiceRouter::enable_set_force);
  if (source_meta_iter != source_metadata.end() && source_meta_iter->second == "true") {
    enable_set_force = true;
  }

  // 未启用set，则直接将路由结果往后透传返回
  if (enable_set_force == false && cache_value->enable_set == false) {
    cache_value->DecrementRef();
    route_result->SetServiceInstances(service_instances);
    route_info.SetServiceInstances(NULL);
    return kReturnOk;
  }

  // 启用set，则禁用nearby路由插件
  route_info.SetRouterFlag(GetIncompatibleServiceRouter(), false);

  // 更新route_result
  cache_value->current_data_->count_++;
  service_instances->UpdateAvailableInstances(cache_value->current_data_);
  cache_value->DecrementRef();
  route_result->SetServiceInstances(service_instances);
  route_info.SetServiceInstances(NULL);
  return kReturnOk;
}

RouterStatData* SetDivisionServiceRouter::CollectStat() { return router_cache_->CollectStat(); }

int SetDivisionServiceRouter::GetHealthyInstances(const std::vector<Instance*>& input,
                                                  const std::set<Instance*>& unhealthy_set,
                                                  std::vector<Instance*>& output) {
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (unhealthy_set.find(input[i]) == unhealthy_set.end()) {
      output.push_back(input[i]);
    }
  }

  return 0;
}

}  // namespace polaris
