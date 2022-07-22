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

#include "cache/service_cache.h"
#include "context/context_impl.h"
#include "logger.h"
#include "model/model_impl.h"
#include "polaris/context.h"
#include "polaris/model.h"

namespace polaris {

class Config;

const char SetDivisionServiceRouter::enable_set_key[] = "internal-enable-set";
const char SetDivisionServiceRouter::enable_set_force[] = "enable-set-force";

SetDivisionServiceRouter::SetDivisionServiceRouter() : router_cache_(nullptr) {}

SetDivisionServiceRouter::~SetDivisionServiceRouter() {
  if (router_cache_ != nullptr) {
    router_cache_->SetClearHandler(0);
    router_cache_->DecrementRef();
    router_cache_ = nullptr;
  }
}

ReturnCode SetDivisionServiceRouter::Init(Config* /*config*/, Context* context) {
  router_cache_ = new ServiceCache<SetDivisionCacheKey, SetDivisionCacheValue>();
  context->GetContextImpl()->RegisterCache(router_cache_);
  return kReturnOk;
}

// 根据主调和被调的metadata判断是否启用set分组
bool SetDivisionServiceRouter::IsSetDivisionRouterEnable(const std::string& caller_set_name,
                                                         const std::string& callee_set_name,
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
  std::string caller_first_set_field = caller_set_name.substr(0, caller_set_name.find_first_of("."));
  std::string callee_first_set_field = callee_set_name.substr(0, callee_set_name.find_first_of("."));
  if (caller_first_set_field == callee_first_set_field) {
    return true;
  }

  return false;
}

int SetDivisionServiceRouter::GetResultWithSetName(std::string set_name, const std::vector<Instance*>& src_instances,
                                                   std::vector<Instance*>& result, bool wild) {
  for (std::size_t i = 0; i < src_instances.size(); ++i) {
    const std::map<std::string, std::string>& metadata = src_instances[i]->GetMetadata();
    std::map<std::string, std::string>::const_iterator iter = metadata.find(enable_set_key);
    // 被调未启用set，则跳过
    if (iter == metadata.end()) {
      continue;
    } else if (strcasecmp("Y", iter->second.c_str())) {  // internal-enable-set != Y/y
      continue;
    }

    // 被调instance的setname
    const std::string& callee_set_name = src_instances[i]->GetInternalSetName();
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
  std::string::size_type last_pos = caller_set_name.find_last_of(".");
  if (first_pos == last_pos || last_pos == std::string::npos) {
    POLARIS_LOG(LOG_ERROR, "exception occur, setname format invalid:%s", caller_set_name.c_str());
    return -1;
  }
  std::string set_name = caller_set_name.substr(0, first_pos);
  std::string set_area = caller_set_name.substr(first_pos + 1, last_pos - first_pos - 1);
  std::string set_group_id = caller_set_name.substr(last_pos + 1);

  if (set_group_id == "*") {
    GetResultWithSetName(set_name + "." + set_area + ".", src_instances, result, true);
  } else {
    GetResultWithSetName(caller_set_name, src_instances, result, false);
    if (result.size() == 0) {
      GetResultWithSetName(set_name + "." + set_area + ".*", src_instances, result, false);
    }
  }

  return 0;
}

ReturnCode SetDivisionServiceRouter::DoRoute(RouteInfo& route_info, RouteResult* route_result) {
  const std::string* caller_set_name = route_info.GetCallerSetName();
  if (nullptr == caller_set_name) {  // 主调没有指定set信息，则认为未启用set
    return kReturnOk;
  }

  // 先从缓存中读取，未找到的话就创建缓存
  SetDivisionCacheKey cache_key;
  ServiceInstances* service_instances = route_info.GetServiceInstances();
  cache_key.prior_data_ = service_instances->GetAvailableInstances();
  cache_key.caller_set_name = *caller_set_name;
  cache_key.circuit_breaker_version_ = route_info.GetCircuitBreakerVersion();

  cache_key.request_flags_ = route_info.GetRequestFlags();
  SetDivisionCacheValue* cache_value = router_cache_->GetWithRcuTime(cache_key);
  if (cache_value == nullptr) {
    cache_value = router_cache_->CreateOrGet(cache_key, [&] {
      SetDivisionCacheValue* new_cache_value = new SetDivisionCacheValue();
      new_cache_value->instances_data_ = service_instances->GetServiceData();
      new_cache_value->instances_data_->IncrementRef();
      new_cache_value->enable_set = false;  // 未强制启用
      // 判断是否启用set逻辑
      InstancesSet* avail_instances = service_instances->GetAvailableInstances();
      for (auto& instance : avail_instances->GetInstances()) {
        if (IsSetDivisionRouterEnable(cache_key.caller_set_name, instance->GetInternalSetName(),
                                      instance->GetMetadata())) {
          new_cache_value->enable_set = true;
          break;
        }
      }

      if (new_cache_value->enable_set == true) {
        // 启用set，则按set逻辑走
        // 从available列表中选出符合条件的instance实例
        std::vector<Instance*> result;
        const std::vector<Instance*>& instances = avail_instances->GetInstances();
        CalculateMatchResult(cache_key.caller_set_name, instances, result);
        // 从选出的列表中进一步选出active的节点，如果没有active的节点，将返回inactive的
        std::set<Instance*> unhealthy_set;
        route_info.CalculateUnhealthySet(unhealthy_set);

        std::vector<Instance*> healthy_result;
        GetHealthyInstances(result, unhealthy_set, healthy_result);
        std::map<std::string, std::string> subset;
        if (!healthy_result.empty()) {
          subset["taf.set"] = cache_key.caller_set_name;
          new_cache_value->current_data_ = new InstancesSet(healthy_result, subset);
        } else {  // 所有节点都死了，则返回所有
          subset["taf.set"] = "*";
          new_cache_value->current_data_ = new InstancesSet(result, subset, "no healthy node");
        }
      } else {
        // 如果判断为启用set的话，则instanceSet设置为空即可
        std::vector<Instance*> empty_result;
        new_cache_value->current_data_ = new InstancesSet(empty_result);
      }

      route_result->SetNewInstancesSet();
      return new_cache_value;
    });
  }

  bool enable_set_force = false;
  std::map<std::string, std::string>& source_metadata = route_info.GetSourceServiceInfo()->metadata_;
  auto source_meta_iter = source_metadata.find(SetDivisionServiceRouter::enable_set_force);
  if (source_meta_iter != source_metadata.end() && source_meta_iter->second == "true") {
    enable_set_force = true;
  }

  // 未启用set，则直接将路由结果往后透传返回
  if (enable_set_force == false && cache_value->enable_set == false) {
    return kReturnOk;
  }

  // 启用set，则禁用nearby路由插件
  route_info.SetNearbyRouterDisable(true);

  // 更新route_result
  cache_value->current_data_->GetImpl()->count_++;
  service_instances->UpdateAvailableInstances(cache_value->current_data_);
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
