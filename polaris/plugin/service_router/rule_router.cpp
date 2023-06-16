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

#include "rule_router.h"

#include <memory>

#include "cache/service_cache.h"
#include "context/context_impl.h"
#include "logger.h"
#include "model/model_impl.h"
#include "model/route_rule.h"
#include "monitor/service_record.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

RuleRouterCluster::~RuleRouterCluster() {
  for (std::map<uint32_t, std::vector<RuleRouterSet*> >::iterator data_it = data_.begin(); data_it != data_.end();
       ++data_it) {
    const std::vector<RuleRouterSet*>& set_data = data_it->second;
    for (std::vector<RuleRouterSet*>::const_iterator set_it = set_data.begin(); set_it != set_data.end(); ++set_it) {
      delete *set_it;
    }
  }
}

// 根据Destination计算路由结果
bool RuleRouterCluster::CalculateByRoute(const RouteRule& route, ServiceKey& service_key, bool match_service,
                                         const std::vector<Instance*>& instances,
                                         const std::set<Instance*>& unhealthy_set,
                                         const std::map<std::string, std::string>& parameters) {
  return route.CalculateSet(service_key, match_service, instances, unhealthy_set, parameters, data_);
}

bool RuleRouterCluster::CalculateRouteResult(std::vector<RuleRouterSet*>& result, uint32_t* sum_weight,
                                             float percent_of_min_instances, bool enable_recover_all) {
  if (data_.empty()) {
    return false;
  }
  // 值越小优先级越高，找到优先级最高的cluster
  std::vector<RuleRouterSet*>& cluster = data_.begin()->second;
  bool downgrade = false;
  do {  // 先选择健康率符合条件的set，如果全部不符合则降级加入健康实例
    for (std::size_t i = 0; i < cluster.size(); ++i) {
      RuleRouterSet* set = cluster[i];
      std::size_t set_size = set->healthy_.size() + set->unhealthy_.size();
      if (downgrade) {  // 把不健康实例加到健康实例
        for (std::size_t j = 0; j < set->unhealthy_.size(); ++j) {
          set->healthy_.push_back(set->unhealthy_[j]);
        }
      }
      if (!set->healthy_.empty() && set->healthy_.size() >= percent_of_min_instances * set_size) {
        result.push_back(set);
        (*sum_weight) += set->weight_;
      }
    }
    if (!result.empty()) {  // 找不到了不用降级
      break;
    }
    // 已经降级寻找了一次，不需要再找了
    if (downgrade) {
      break;
    }
    if (enable_recover_all) {
      downgrade = true;  // 降级再找一次
    } else {             // 不开启全死全活则直接退出
      break;
    }
  } while (result.empty());
  return downgrade;
}

void RuleRouterCluster::CalculateSubset(ServiceInstances* service_instances, Labels& labels) {
  // 检查最高优先级的subset，状态健康则返回其中实例，如果同优先级的有多个subset，则根据权重计算返回实例
  // 如果最高优先级subset不健康，那么按照优先级遍历检查其他subset，如果有健康的，返回其中实例，如有多个同优先级的subset则按权重计算
  // 若无健康的低优先级subset，则无论最高优先级subset健康与否，都返回其中实例
  // 如果是熔断半开状态，要支持按比例放量
  //  PRESERVED：熔断器维持，节点处于保守状态，此时只能正常接收自身流量，不可接受别的集群降级流量

  // 为空，直接返回
  // 剔除不健康的subset
  if (data_.empty()) {
    return;
  }
  std::map<uint32_t, std::vector<RuleRouterSet*> >::iterator data_it = data_.begin();
  std::vector<RuleRouterSet*>& cluster_highest_priority = data_it->second;

  // 通过service_instances直接获取熔断set信息
  std::map<std::string, SetCircuitBreakerUnhealthyInfo> circuit_breaker_sets;
  if (service_instances->GetService() == nullptr) {
    POLARIS_LOG(LOG_ERROR, "Service member is null");
  } else {
    circuit_breaker_sets = service_instances->GetService()->GetCircuitBreakerSetUnhealthySets();
  }

  std::vector<RuleRouterSet*> cluster_halfopen;
  if (GetHealthyAndHalfOpenSubSet(cluster_highest_priority, circuit_breaker_sets, cluster_halfopen, labels)) {
    // 如果有半开的，考虑降级
    for (std::vector<RuleRouterSet*>::iterator set_it = cluster_halfopen.begin(); set_it != cluster_halfopen.end();
         ++set_it) {
      RuleRouterSet* downgrade = GetDownGradeSubset(circuit_breaker_sets, labels);
      if (downgrade) {
        // 根据半开放量率计算比例
        float pass_rate = 1.0;
        SetCircuitBreakerUnhealthyInfo* breaker_info = nullptr;
        GetSetBreakerInfo(circuit_breaker_sets, (*set_it)->subset, labels, &breaker_info);
        if (breaker_info != nullptr) {
          pass_rate = breaker_info->half_open_release_percent;
        }

        // 修改权重
        (*set_it)->weight_ *= pass_rate;
        downgrade->weight_ *= (1 - pass_rate);
        cluster_highest_priority.push_back(*set_it);
        cluster_highest_priority.push_back(downgrade);
      } else {
        cluster_highest_priority.push_back(*set_it);
      }
    }
    return;
  }
  // 没有
  // 降级寻找,只找健康的set，否则返回最高优先级set
  ++data_it;
  for (; data_it != data_.end(); ++data_it) {
    if (GetHealthySubSet(data_it->second, circuit_breaker_sets, labels)) {
      break;
    }
  }

  if (data_it == data_.end()) {
    // 依旧取最高优先级的实例,此处什么也不用做
    return;
  } else {
    // 找到一个可替换的优先级subset组
    std::map<uint32_t, std::vector<RuleRouterSet*> > data_tmp;
    data_tmp[data_it->first].swap(data_it->second);
    data_.swap(data_tmp);
    // 释放Set指针的内存
    for (std::map<uint32_t, std::vector<RuleRouterSet*> >::iterator tmp_it = data_tmp.begin(); tmp_it != data_tmp.end();
         ++tmp_it) {
      const std::vector<RuleRouterSet*>& set_data = tmp_it->second;
      for (std::vector<RuleRouterSet*>::const_iterator set_it = set_data.begin(); set_it != set_data.end(); ++set_it) {
        delete *set_it;
      }
    }
  }
}

void RuleRouterCluster::GetSetBreakerInfo(std::map<std::string, SetCircuitBreakerUnhealthyInfo>& circuit_breaker_sets,
                                          SubSetInfo& subset, Labels& labels,
                                          SetCircuitBreakerUnhealthyInfo** breaker_info) {
  std::string all_subset_key = subset.GetSubInfoStrId() + "#";
  std::map<std::string, SetCircuitBreakerUnhealthyInfo>::iterator all_it = circuit_breaker_sets.find(all_subset_key);
  if (all_it != circuit_breaker_sets.end()) {
    // subset被熔断，直接获取返回
    *breaker_info = &all_it->second;
    return;
  }
  std::string subset_key = subset.GetSubInfoStrId() + "#" + labels.GetLabelStr();
  std::map<std::string, SetCircuitBreakerUnhealthyInfo>::iterator subset_it = circuit_breaker_sets.find(subset_key);
  if (subset_it != circuit_breaker_sets.end()) {
    // 接口级熔断
    *breaker_info = &subset_it->second;
  }
}

RuleRouterSet* RuleRouterCluster::GetDownGradeSubset(
    std::map<std::string, SetCircuitBreakerUnhealthyInfo>& circuit_breaker_sets, Labels& labels) {
  // 为空或者只有一个优先级组，都直接返回
  // 寻找一个健康的降级subset
  if (data_.size() <= 1) {
    return nullptr;
  }
  std::map<uint32_t, std::vector<RuleRouterSet*> >::iterator data_it = data_.begin();
  ++data_it;
  for (; data_it != data_.end(); ++data_it) {
    for (std::vector<RuleRouterSet*>::iterator it = data_it->second.begin(); it != data_it->second.end(); ++it) {
      CircuitBreakerStatus cbs = kCircuitBreakerClose;
      SetCircuitBreakerUnhealthyInfo* breaker_info = nullptr;
      GetSetBreakerInfo(circuit_breaker_sets, (*it)->subset, labels, &breaker_info);
      if (breaker_info != nullptr) {
        cbs = breaker_info->status;
      }
      if (!(*it)->isolated_ && cbs == kCircuitBreakerClose) {
        // 要从vector中移除，免得重复释放，用sharedptr比较好
        RuleRouterSet* result = *it;
        data_it->second.erase(it);
        return result;
      }
    }
  }
  return nullptr;
}

bool RuleRouterCluster::GetHealthyAndHalfOpenSubSet(
    std::vector<RuleRouterSet*>& cluster, std::map<std::string, SetCircuitBreakerUnhealthyInfo>& circuit_breaker_sets,
    std::vector<RuleRouterSet*>& cluster_halfopen, Labels& labels) {
  if (cluster.empty()) {
    return false;
  }
  // 判断subset状态是否健康，剔除不健康的set
  std::vector<RuleRouterSet*> cluster_healthy;
  std::vector<RuleRouterSet*> cluster_unhealthy;
  std::vector<RuleRouterSet*> cluster_isolated;

  for (std::vector<RuleRouterSet*>::iterator it = cluster.begin(); it != cluster.end(); ++it) {
    CircuitBreakerStatus cbs = kCircuitBreakerClose;
    SetCircuitBreakerUnhealthyInfo* breaker_info = nullptr;
    GetSetBreakerInfo(circuit_breaker_sets, (*it)->subset, labels, &breaker_info);
    if (breaker_info != nullptr) {
      cbs = breaker_info->status;
    }

    if ((*it)->isolated_) {
      cluster_isolated.push_back(*it);
    } else if (cbs == kCircuitBreakerClose || cbs == kCircuitBreakerPreserved) {
      cluster_healthy.push_back(*it);
    } else if (cbs == kCircuitBreakerHalfOpen) {
      cluster_halfopen.push_back(*it);
    } else {
      cluster_unhealthy.push_back(*it);
    }
  }
  // 移除隔离的set
  for (std::vector<RuleRouterSet*>::iterator it = cluster_isolated.begin(); it != cluster_isolated.end(); ++it) {
    delete *it;
  }

  if (!cluster_healthy.empty() || !cluster_halfopen.empty()) {
    // 只保留健康和半开的set
    // 注意内存释放！
    cluster.swap(cluster_healthy);
    for (std::vector<RuleRouterSet*>::iterator it = cluster_unhealthy.begin(); it != cluster_unhealthy.end(); ++it) {
      delete *it;
    }
    return true;
  }
  // 没有健康的set
  // 隔离的set已经被释放
  cluster.swap(cluster_unhealthy);
  return false;
}

bool RuleRouterCluster::GetHealthySubSet(std::vector<RuleRouterSet*>& cluster,
                                         std::map<std::string, SetCircuitBreakerUnhealthyInfo>& circuit_breaker_sets,
                                         Labels& labels) {
  if (cluster.empty()) {
    return false;
  }
  std::vector<RuleRouterSet*> cluster_healthy;
  std::vector<RuleRouterSet*> cluster_unhealthy;
  for (std::vector<RuleRouterSet*>::iterator it = cluster.begin(); it != cluster.end(); ++it) {
    CircuitBreakerStatus cbs = kCircuitBreakerClose;
    SetCircuitBreakerUnhealthyInfo* breaker_info = nullptr;
    GetSetBreakerInfo(circuit_breaker_sets, (*it)->subset, labels, &breaker_info);
    if (breaker_info != nullptr) {
      cbs = breaker_info->status;
    }
    if (!(*it)->isolated_ && cbs == kCircuitBreakerClose) {
      cluster_healthy.push_back(*it);
    } else {
      cluster_unhealthy.push_back(*it);
    }
  }
  if (!cluster_healthy.empty()) {
    cluster.swap(cluster_healthy);
    for (std::vector<RuleRouterSet*>::iterator it = cluster_unhealthy.begin(); it != cluster_unhealthy.end(); ++it) {
      delete *it;
    }
    return true;
  }
  return false;
}

RuleServiceRouter::RuleServiceRouter() : not_match_count_(0) {
  enable_recover_all_ = true;
  percent_of_min_instances_ = 0;
  router_cache_ = nullptr;
  context_ = nullptr;
}

RuleServiceRouter::~RuleServiceRouter() {
  if (router_cache_ != nullptr) {
    router_cache_->SetClearHandler(0);
    router_cache_->DecrementRef();
    router_cache_ = nullptr;
  }
  context_ = nullptr;
}

ReturnCode RuleServiceRouter::Init(Config* config, Context* context) {
  static const char kRecoverAllEnableKey[] = "enableRecoverAll";
  static const bool kRecoverAllEnableDefault = true;
  static const char kPercentOfMinInstancesKey[] = "percentOfMinInstances";
  static const float kPercentOfMinInstancesDefault = 0.0;

  enable_recover_all_ = config->GetBoolOrDefault(kRecoverAllEnableKey, kRecoverAllEnableDefault);
  percent_of_min_instances_ = config->GetFloatOrDefault(kPercentOfMinInstancesKey, kPercentOfMinInstancesDefault);
  router_cache_ = new ServiceCache<RuleRouteCacheKey, RuleRouterCacheValue>();
  context_ = context;
  context_->GetContextImpl()->RegisterCache(router_cache_);
  return kReturnOk;
}

std::string RuleServiceRouter::Name() { return "RuleServiceRouter"; }

ReturnCode RuleServiceRouter::DoRoute(RouteInfo& route_info, RouteResult* route_result) {
  ServiceRouteRule* route_rule = route_info.GetServiceRouteRule();
  POLARIS_CHECK_ARGUMENT(route_rule != nullptr);
  // source_service_info为NULL的时候，source_route_rule必须为NULL
  // source_service_info不为NULL的时候有三种情况：
  //  1.source_service_info设置了metadata没有service_key：不需要源服务路由，走目标服务路由
  //  2.source_service_info设置了service_key和metadata：先走源服务路由再走目标服务路由
  //  3.source_service_info同1，但传入了自己构造的source_route_rule：给trpc-cpp使用
  ServiceInfo* source_service_info = route_info.GetSourceServiceInfo();
  ServiceRouteRule* source_route_rule = route_info.GetSourceServiceRouteRule();
  if (source_service_info == nullptr) {
    POLARIS_CHECK_ARGUMENT(source_route_rule == nullptr);
  }

  RouteRuleBound* matched_route = nullptr;
  bool match_outbounds = true;  // 是否匹配的源服务的出规则
  RuleRouteCacheKey cache_key;
  if (!ServiceRouteRule::RouteMatch(route_rule, route_info.GetServiceKey(), source_route_rule, source_service_info,
                                    matched_route, &match_outbounds, cache_key.parameters_)) {
    not_match_count_++;
    return kReturnRouteRuleNotMatch;
  }

  if (matched_route != nullptr) {  // 匹配到了规则，则需要根据规则计算
    // 先查缓存，缓存不存在再计算
    ServiceInstances* service_instances = route_info.GetServiceInstances();
    cache_key.prior_data_ = service_instances->GetAvailableInstances();
    cache_key.route_key_ = matched_route;
    cache_key.request_flags_ = route_info.GetRequestFlags();
    cache_key.circuit_breaker_version_ = route_info.GetCircuitBreakerVersion();
    cache_key.subset_circuit_breaker_version_ =
        service_instances->GetService()->GetCircuitBreakerSetUnhealthyDataVersion();
    Labels labels;
    labels.labels_ = route_info.GetLabels();
    cache_key.labels_ = labels.GetLabelStr();

    RuleRouterCacheValue* cache_value = router_cache_->GetWithRcuTime(cache_key);
    if (cache_value == nullptr) {
      cache_value = router_cache_->CreateOrGet(cache_key, [&] {
        // 获取熔断实例和不健康实例
        std::set<Instance*> unhealthy_set;
        route_info.CalculateUnhealthySet(unhealthy_set);
        InstancesSet* available_set = service_instances->GetAvailableInstances();
        RuleRouterCluster rule_router_cluster;
        bool calculate_result;
        ServiceKey service_key = route_info.GetServiceKey();  // 复制，路由匹配时可能会修改成转发的服务
        if (source_service_info == nullptr) {
          std::map<std::string, std::string> parameters;
          calculate_result =
              rule_router_cluster.CalculateByRoute(matched_route->route_rule_, service_key, match_outbounds,
                                                   available_set->GetInstances(), unhealthy_set, parameters);
        } else {
          calculate_result = rule_router_cluster.CalculateByRoute(matched_route->route_rule_, service_key,
                                                                  match_outbounds, available_set->GetInstances(),
                                                                  unhealthy_set, source_service_info->metadata_);
        }
        RuleRouterCacheValue* new_cache_value = new RuleRouterCacheValue();
        route_result->SetNewInstancesSet();
        if (!calculate_result) {
          new_cache_value->is_redirect_ = true;
          new_cache_value->redirect_service_ = service_key;  // 转发
          return new_cache_value;
        }
        // subset处理, 需要用到serviceContext来判断subset状态
        rule_router_cluster.CalculateSubset(service_instances, labels);
        std::vector<RuleRouterSet*> result;
        uint32_t sum_weight = 0;
        bool recover_all = rule_router_cluster.CalculateRouteResult(result, &sum_weight, percent_of_min_instances_,
                                                                    enable_recover_all_);
        if (result.empty()) {
          return new_cache_value;
        }
        new_cache_value->instances_data_ = service_instances->GetServiceData();
        new_cache_value->instances_data_->IncrementRef();
        new_cache_value->route_rule_ =
            match_outbounds ? source_route_rule->GetServiceData() : route_rule->GetServiceData();
        new_cache_value->route_rule_->IncrementRef();
        new_cache_value->subset_sum_weight_ = 0;
        new_cache_value->match_outbounds_ = match_outbounds;
        std::string select_cluster;
        for (std::size_t i = 0; i < result.size(); ++i) {
          if (sum_weight <= 0) {  // 全部没有权重，则使用默认权重
            result[i]->weight_ = 100;
          } else if (result[i]->weight_ <= 0) {  // 有部分有权重，则过滤权重为0的分组
            continue;
          }
          new_cache_value->subset_sum_weight_ += result[i]->weight_;
          InstancesSet* set = new InstancesSet(result[i]->healthy_, result[i]->subset.subset_map_);
          new_cache_value->subsets_.insert(std::make_pair(new_cache_value->subset_sum_weight_, set));
          select_cluster += result[i]->subset.GetSubInfoStrId() + ",";
        }
        bool old_recover_all = matched_route->recover_all_.load();
        if (recover_all != old_recover_all) {  // 本次计算发生了全死全活变化，尝试修改记录标志
          if (matched_route->recover_all_.compare_exchange_strong(old_recover_all, recover_all)) {
            context_->GetContextImpl()->GetServiceRecord()->InstanceRecoverAll(
                route_info.GetServiceKey(), new RecoverAllRecord(Time::GetSystemTimeMs(), select_cluster, recover_all));
          }
        }
        return new_cache_value;
      });
    }
    if (cache_value->is_redirect_) {
      route_result->SetRedirectService(cache_value->redirect_service_);
      return kReturnOk;
    } else if (cache_value->subset_sum_weight_ == 0) {
      not_match_count_++;
      return kReturnRouteRuleNotMatch;
    }

    InstancesSet* instances_result =
        ServiceRouteRule::SelectSet(cache_value->subsets_, cache_value->subset_sum_weight_);
    service_instances->UpdateAvailableInstances(instances_result);
    route_result->SetSubset(instances_result->GetSubset());
  }
  return kReturnOk;
}

RouterStatData* RuleServiceRouter::CollectStat() {
  RouterStatData* data = nullptr;
  int count = not_match_count_.exchange(0);
  if (count > 0) {
    data = new RouterStatData();
    v1::RouteResult* result = data->record_.add_results();
    result->set_ret_code("ErrCodeRouteRuleNotMatch");
    result->set_period_times(count);
  }
  std::vector<RuleRouterCacheValue*> values;
  router_cache_->GetAllValuesWithRef(values);
  for (std::size_t i = 0; i < values.size(); ++i) {
    RuleRouterCacheValue* value = values[i];
    bool have_data = false;
    for (auto it = value->subsets_.begin(); it != value->subsets_.end(); ++it) {
      if ((count = it->second->GetImpl()->count_.exchange(0)) > 0) {
        if (data == nullptr) {
          data = new RouterStatData();
        }
        have_data = true;
        v1::RouteResult* result = data->record_.add_results();
        result->set_ret_code("Success");
        result->set_period_times(count);
        result->set_cluster(StringUtils::MapToStr(it->second->GetSubset()));
        result->set_route_status(it->second->GetRecoverInfo());
      }
      if (have_data) {
        data->record_.set_rule_type(value->match_outbounds_ ? v1::RouteRecord::DestRule : v1::RouteRecord::SrcRule);
      }
    }
    value->DecrementRef();
  }
  return data;
}

}  // namespace polaris
