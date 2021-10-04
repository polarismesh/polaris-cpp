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

#include <google/protobuf/map.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/wrappers.pb.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include <v1/model.pb.h>
#include <v1/routing.pb.h>

#include <memory>
#include <utility>

#include "cache/service_cache.h"
#include "context_internal.h"
#include "logger.h"
#include "model/model_impl.h"
#include "model/route_rule.h"
#include "monitor/service_record.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "service_router.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

RuleRouterCluster::~RuleRouterCluster() {
  for (std::map<uint32_t, std::vector<RuleRouterSet*> >::iterator data_it = data_.begin();
       data_it != data_.end(); ++data_it) {
    const std::vector<RuleRouterSet*>& set_data = data_it->second;
    for (std::vector<RuleRouterSet*>::const_iterator set_it = set_data.begin();
         set_it != set_data.end(); ++set_it) {
      delete *set_it;
    }
  }
}

// 根据Destination计算路由结果
bool RuleRouterCluster::CalculateByRoute(const RouteRule& route, ServiceKey& service_key,
                                         bool match_service,
                                         const std::vector<Instance*>& instances,
                                         const std::set<Instance*>& unhealthy_set,
                                         const std::map<std::string, std::string>& parameters) {
  return route.CalculateSet(service_key, match_service, instances, unhealthy_set, parameters,
                            data_);
}

bool RuleRouterCluster::CalculateRouteResult(std::vector<RuleRouterSet*>& result,
                                             uint32_t* sum_weight, float percent_of_min_instances,
                                             bool enable_recover_all) {
  if (data_.empty()) {
    return false;
  }
  // 值越小优先级越高，找到优先级最高的cluster
  std::vector<RuleRouterSet*>& cluster = data_.begin()->second;
  bool downgrade                       = false;
  do {  // 先选择健康率符合条件的set，如果全部不符合则降级加入健康实例
    for (std::size_t i = 0; i < cluster.size(); ++i) {
      RuleRouterSet* set   = cluster[i];
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
    //已经降级寻找了一次，不需要再找了
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
  //检查最高优先级的subset，状态健康则返回其中实例，如果同优先级的有多个subset，则根据权重计算返回实例
  //如果最高优先级subset不健康，那么按照优先级遍历检查其他subset，如果有健康的，返回其中实例，如有多个同优先级的subset则按权重计算
  //若无健康的低优先级subset，则无论最高优先级subset健康与否，都返回其中实例
  //如果是熔断半开状态，要支持按比例放量
  // PRESERVED：熔断器维持，节点处于保守状态，此时只能正常接收自身流量，不可接受别的集群降级流量

  //为空，直接返回
  //剔除不健康的subset
  if (data_.empty()) {
    return;
  }
  std::map<uint32_t, std::vector<RuleRouterSet*> >::iterator data_it = data_.begin();
  std::vector<RuleRouterSet*>& cluster_highest_priority              = data_it->second;

  //通过service_instances直接获取熔断set信息
  std::map<std::string, SetCircuitBreakerUnhealthyInfo> circuit_breaker_sets;
  if (service_instances->GetService() == NULL) {
    POLARIS_LOG(LOG_ERROR, "Service member is null");
  } else {
    circuit_breaker_sets = service_instances->GetService()->GetCircuitBreakerSetUnhealthySets();
  }

  std::vector<RuleRouterSet*> cluster_halfopen;
  if (GetHealthyAndHalfOpenSubSet(cluster_highest_priority, circuit_breaker_sets, cluster_halfopen,
                                  labels)) {
    //如果有半开的，考虑降级
    for (std::vector<RuleRouterSet*>::iterator set_it = cluster_halfopen.begin();
         set_it != cluster_halfopen.end(); ++set_it) {
      RuleRouterSet* downgrade = GetDownGradeSubset(circuit_breaker_sets, labels);
      if (downgrade) {
        //根据半开放量率计算比例
        float pass_rate                              = 1.0;
        SetCircuitBreakerUnhealthyInfo* breaker_info = NULL;
        GetSetBreakerInfo(circuit_breaker_sets, (*set_it)->subset, labels, &breaker_info);
        if (breaker_info != NULL) {
          pass_rate = breaker_info->half_open_release_percent;
        }

        //修改权重
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
  //没有
  //降级寻找,只找健康的set，否则返回最高优先级set
  ++data_it;
  for (; data_it != data_.end(); ++data_it) {
    if (GetHealthySubSet(data_it->second, circuit_breaker_sets, labels)) {
      break;
    }
  }

  if (data_it == data_.end()) {
    //依旧取最高优先级的实例,此处什么也不用做
    return;
  } else {
    //找到一个可替换的优先级subset组
    std::map<uint32_t, std::vector<RuleRouterSet*> > data_tmp;
    data_tmp[data_it->first].swap(data_it->second);
    data_.swap(data_tmp);
    //释放Set指针的内存
    for (std::map<uint32_t, std::vector<RuleRouterSet*> >::iterator tmp_it = data_tmp.begin();
         tmp_it != data_tmp.end(); ++tmp_it) {
      const std::vector<RuleRouterSet*>& set_data = tmp_it->second;
      for (std::vector<RuleRouterSet*>::const_iterator set_it = set_data.begin();
           set_it != set_data.end(); ++set_it) {
        delete *set_it;
      }
    }
  }
}

void RuleRouterCluster::GetSetBreakerInfo(
    std::map<std::string, SetCircuitBreakerUnhealthyInfo>& circuit_breaker_sets, SubSetInfo& subset,
    Labels& labels, SetCircuitBreakerUnhealthyInfo** breaker_info) {
  std::string all_subset_key = subset.GetSubInfoStrId() + "#";
  std::map<std::string, SetCircuitBreakerUnhealthyInfo>::iterator all_it =
      circuit_breaker_sets.find(all_subset_key);
  if (all_it != circuit_breaker_sets.end()) {
    // subset被熔断，直接获取返回
    *breaker_info = &all_it->second;
    return;
  }
  std::string subset_key = subset.GetSubInfoStrId() + "#" + labels.GetLabelStr();
  std::map<std::string, SetCircuitBreakerUnhealthyInfo>::iterator subset_it =
      circuit_breaker_sets.find(subset_key);
  if (subset_it != circuit_breaker_sets.end()) {
    //接口级熔断
    *breaker_info = &subset_it->second;
  }
}

RuleRouterSet* RuleRouterCluster::GetDownGradeSubset(
    std::map<std::string, SetCircuitBreakerUnhealthyInfo>& circuit_breaker_sets, Labels& labels) {
  //为空或者只有一个优先级组，都直接返回
  //寻找一个健康的降级subset
  if (data_.size() <= 1) {
    return NULL;
  }
  std::map<uint32_t, std::vector<RuleRouterSet*> >::iterator data_it = data_.begin();
  ++data_it;
  for (; data_it != data_.end(); ++data_it) {
    for (std::vector<RuleRouterSet*>::iterator it = data_it->second.begin();
         it != data_it->second.end(); ++it) {
      CircuitBreakerStatus cbs                     = kCircuitBreakerClose;
      SetCircuitBreakerUnhealthyInfo* breaker_info = NULL;
      GetSetBreakerInfo(circuit_breaker_sets, (*it)->subset, labels, &breaker_info);
      if (breaker_info != NULL) {
        cbs = breaker_info->status;
      }
      if (!(*it)->isolated_ && cbs == kCircuitBreakerClose) {
        //要从vector中移除，免得重复释放，用sharedptr比较好
        RuleRouterSet* result = *it;
        data_it->second.erase(it);
        return result;
      }
    }
  }
  return NULL;
}

bool RuleRouterCluster::GetHealthyAndHalfOpenSubSet(
    std::vector<RuleRouterSet*>& cluster,
    std::map<std::string, SetCircuitBreakerUnhealthyInfo>& circuit_breaker_sets,
    std::vector<RuleRouterSet*>& cluster_halfopen, Labels& labels) {
  if (cluster.empty()) {
    return false;
  }
  //判断subset状态是否健康，剔除不健康的set
  std::vector<RuleRouterSet*> cluster_healthy;
  std::vector<RuleRouterSet*> cluster_unhealthy;
  std::vector<RuleRouterSet*> cluster_isolated;

  for (std::vector<RuleRouterSet*>::iterator it = cluster.begin(); it != cluster.end(); ++it) {
    CircuitBreakerStatus cbs                     = kCircuitBreakerClose;
    SetCircuitBreakerUnhealthyInfo* breaker_info = NULL;
    GetSetBreakerInfo(circuit_breaker_sets, (*it)->subset, labels, &breaker_info);
    if (breaker_info != NULL) {
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
  //移除隔离的set
  for (std::vector<RuleRouterSet*>::iterator it = cluster_isolated.begin();
       it != cluster_isolated.end(); ++it) {
    delete *it;
  }

  if (!cluster_healthy.empty() || !cluster_halfopen.empty()) {
    //只保留健康和半开的set
    //注意内存释放！
    cluster.swap(cluster_healthy);
    for (std::vector<RuleRouterSet*>::iterator it = cluster_unhealthy.begin();
         it != cluster_unhealthy.end(); ++it) {
      delete *it;
    }
    return true;
  }
  //没有健康的set
  //隔离的set已经被释放
  cluster.swap(cluster_unhealthy);
  return false;
}

bool RuleRouterCluster::GetHealthySubSet(
    std::vector<RuleRouterSet*>& cluster,
    std::map<std::string, SetCircuitBreakerUnhealthyInfo>& circuit_breaker_sets, Labels& labels) {
  if (cluster.empty()) {
    return false;
  }
  std::vector<RuleRouterSet*> cluster_healthy;
  std::vector<RuleRouterSet*> cluster_unhealthy;
  for (std::vector<RuleRouterSet*>::iterator it = cluster.begin(); it != cluster.end(); ++it) {
    CircuitBreakerStatus cbs                     = kCircuitBreakerClose;
    SetCircuitBreakerUnhealthyInfo* breaker_info = NULL;
    GetSetBreakerInfo(circuit_breaker_sets, (*it)->subset, labels, &breaker_info);
    if (breaker_info != NULL) {
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
    for (std::vector<RuleRouterSet*>::iterator it = cluster_unhealthy.begin();
         it != cluster_unhealthy.end(); ++it) {
      delete *it;
    }
    return true;
  }
  return false;
}

RuleServiceRouter::RuleServiceRouter() {
  enable_recover_all_       = true;
  percent_of_min_instances_ = 0;
  router_cache_             = NULL;
  context_                  = NULL;
}

RuleServiceRouter::~RuleServiceRouter() {
  if (router_cache_ != NULL) {
    router_cache_->SetClearHandler(0);
    router_cache_->DecrementRef();
    router_cache_ = NULL;
  }
  context_ = NULL;
}

ReturnCode RuleServiceRouter::Init(Config* config, Context* context) {
  enable_recover_all_ = config->GetBoolOrDefault(ServiceRouterConfig::kRecoverAllEnableKey,
                                                 ServiceRouterConfig::kRecoverAllEnableDefault);
  percent_of_min_instances_ =
      config->GetFloatOrDefault(ServiceRouterConfig::kPercentOfMinInstancesKey,
                                ServiceRouterConfig::kPercentOfMinInstancesDefault);
  router_cache_ = new ServiceCache<RuleRouteCacheKey>();
  context_      = context;
  context_->GetContextImpl()->RegisterCache(router_cache_);
  return kReturnOk;
}

ReturnCode RuleServiceRouter::DoRoute(RouteInfo& route_info, RouteResult* route_result) {
  POLARIS_CHECK_ARGUMENT(route_result != NULL);

  ServiceInstances* service_instances = route_info.GetServiceInstances();
  ServiceRouteRule* route_rule        = route_info.GetServiceRouteRule();
  ServiceInfo* source_service_info    = route_info.GetSourceServiceInfo();
  ServiceRouteRule* source_route_rule = route_info.GetSourceServiceRouteRule();
  POLARIS_CHECK_ARGUMENT(service_instances != NULL);
  POLARIS_CHECK_ARGUMENT(route_rule != NULL);
  // source_service_info为NULL的时候，source_route_rule必须为NULL
  // source_service_info不为NULL的时候有三种情况：
  //  1.source_service_info设置了metadata没有service_key：不需要源服务路由，走目标服务路由
  //  2.source_service_info设置了service_key和metadata：先走源服务路由再走目标服务路由
  //  3.source_service_info同1，但传入了自己构造的source_route_rule：给trpc-cpp使用
  if (source_service_info == NULL) {
    POLARIS_CHECK_ARGUMENT(source_route_rule == NULL);
  }

  RouteRuleBound* matched_route = NULL;
  bool match_outbounds          = true;  // 是否匹配的源服务的出规则
  bool have_route_rule          = false;
  RuleRouteCacheKey cache_key;
  if (!RouteMatch(route_rule, source_route_rule, source_service_info, matched_route,
                  &match_outbounds, &have_route_rule, cache_key.parameters_)) {
    not_match_count_++;
    return kReturnRouteRuleNotMatch;
  }

  if (matched_route != NULL) {  // 匹配到了规则，则需要根据规则计算
    // 先查缓存，缓存不存在再计算
    cache_key.prior_data_    = service_instances->GetAvailableInstances();
    cache_key.route_key_     = matched_route;
    cache_key.request_flags_ = route_info.GetRequestFlags();
    cache_key.circuit_breaker_version_ =
        service_instances->GetService()->GetCircuitBreakerDataVersion();
    cache_key.subset_circuit_breaker_version_ =
        service_instances->GetService()->GetCircuitBreakerSetUnhealthyDataVersion();
    Labels labels;
    labels.labels_    = route_info.GetLabels();
    cache_key.labels_ = labels.GetLabelStr();

    CacheValueBase* cache_value_base  = router_cache_->GetWithRef(cache_key);
    RuleRouterCacheValue* cache_value = NULL;
    if (cache_value_base != NULL) {
      cache_value = dynamic_cast<RuleRouterCacheValue*>(cache_value_base);
      POLARIS_ASSERT(cache_value != NULL);
    } else {
      ServiceKey service_key = route_info.GetServiceKey();
      std::set<Instance*> unhealthy_set;
      // 获取熔断实例和不健康实例
      CalculateUnhealthySet(route_info, service_instances, unhealthy_set);
      InstancesSet* available_set = service_instances->GetAvailableInstances();
      RuleRouterCluster rule_router_cluster;
      bool calculate_result;
      if (source_service_info == NULL) {
        std::map<std::string, std::string> parameters;
        calculate_result = rule_router_cluster.CalculateByRoute(
            matched_route->route_rule_, service_key, match_outbounds, available_set->GetInstances(),
            unhealthy_set, parameters);
      } else {
        calculate_result = rule_router_cluster.CalculateByRoute(
            matched_route->route_rule_, service_key, match_outbounds, available_set->GetInstances(),
            unhealthy_set, source_service_info->metadata_);
      }
      if (!calculate_result) {
        route_result->SetRedirectService(service_key);  // 转发
        return kReturnOk;
      }
      // subset处理, 需要用到serviceContext来判断subset状态
      rule_router_cluster.CalculateSubset(service_instances, labels);
      std::vector<RuleRouterSet*> result;
      uint32_t sum_weight = 0;
      bool recover_all    = rule_router_cluster.CalculateRouteResult(
          result, &sum_weight, percent_of_min_instances_, enable_recover_all_);
      if (result.empty()) {
        not_match_count_++;
        return kReturnRouteRuleNotMatch;
      }
      cache_value                  = new RuleRouterCacheValue();
      cache_value->instances_data_ = service_instances->GetServiceData();
      cache_value->instances_data_->IncrementRef();
      cache_value->route_rule_ =
          match_outbounds ? source_route_rule->GetServiceData() : route_rule->GetServiceData();
      cache_value->route_rule_->IncrementRef();
      cache_value->sum_weight_      = 0;
      cache_value->match_outbounds_ = match_outbounds;
      std::string select_cluster;
      for (std::size_t i = 0; i < result.size(); ++i) {
        if (sum_weight <= 0) {  // 全部没有权重，则使用默认权重
          result[i]->weight_ = 100;
        } else if (result[i]->weight_ <= 0) {  // 有部分有权重，则过滤权重为0的分组
          continue;
        }
        cache_value->sum_weight_ += result[i]->weight_;
        InstancesSet* set = new InstancesSet(result[i]->healthy_, result[i]->subset.subset_map_);
        cache_value->data_.insert(std::make_pair(cache_value->sum_weight_, set));
        select_cluster += result[i]->subset.GetSubInfoStrId() + ",";
      }
      router_cache_->PutWithRef(cache_key, cache_value);
      if (recover_all) {  // 本次计算发生了全死全活，且cluster未记录，尝试修改记录标志
        if (!matched_route->recover_all_ && ATOMIC_CAS(&matched_route->recover_all_, false, true)) {
          context_->GetContextImpl()->GetServiceRecord()->InstanceRecoverAll(
              service_key, new RecoverAllRecord(Time::GetCurrentTimeMs(), select_cluster, true));
        }
      } else {  // 本次计算未发生全死全活，检查之前是否发生了全死全活
        if (matched_route->recover_all_ && ATOMIC_CAS(&matched_route->recover_all_, false, true)) {
          context_->GetContextImpl()->GetServiceRecord()->InstanceRecoverAll(
              service_key, new RecoverAllRecord(Time::GetCurrentTimeMs(), select_cluster, false));
        }
      }
    }
    POLARIS_ASSERT(cache_value->sum_weight_ > 0);
    InstancesSet* instances_result = SelectSet(cache_value->data_, cache_value->sum_weight_);
    service_instances->UpdateAvailableInstances(instances_result);
    route_result->SetSubset(instances_result->GetSubset());
    cache_value->DecrementRef();
  }
  route_result->SetServiceInstances(service_instances);
  route_info.SetServiceInstances(NULL);
  return kReturnOk;
}

InstancesSet* RuleServiceRouter::SelectSet(std::map<uint32_t, InstancesSet*>& cluster,
                                           uint32_t sum_weight) {
  static __thread bool thread_local_seed_not_init = true;
  static __thread unsigned int thread_local_seed  = 0;
  if (thread_local_seed_not_init) {
    thread_local_seed_not_init = false;
    thread_local_seed          = time(NULL) ^ pthread_self();
  }
  uint32_t random_weight                         = rand_r(&thread_local_seed) % sum_weight;
  std::map<uint32_t, InstancesSet*>::iterator it = cluster.upper_bound(random_weight);
  it->second->GetInstancesSetImpl()->count_++;
  return it->second;
}

RouterStatData* RuleServiceRouter::CollectStat() {
  RouterStatData* data = NULL;
  int count            = not_match_count_.Exchange(0);
  if (count > 0) {
    data                    = new RouterStatData();
    v1::RouteResult* result = data->record_.add_results();
    result->set_ret_code("ErrCodeRouteRuleNotMatch");
    result->set_period_times(count);
  }
  std::vector<CacheValueBase*> values;
  router_cache_->GetAllValuesWithRef(values);
  for (std::size_t i = 0; i < values.size(); ++i) {
    RuleRouterCacheValue* value = dynamic_cast<RuleRouterCacheValue*>(values[i]);
    POLARIS_ASSERT(value != NULL);
    bool have_data = false;
    for (std::map<uint32_t, InstancesSet*>::iterator it = value->data_.begin();
         it != value->data_.end(); ++it) {
      if ((count = it->second->GetInstancesSetImpl()->count_.Exchange(0)) > 0) {
        if (data == NULL) {
          data = new RouterStatData();
        }
        have_data               = true;
        v1::RouteResult* result = data->record_.add_results();
        result->set_ret_code("Success");
        result->set_period_times(count);
        result->set_cluster(StringUtils::MapToStr(it->second->GetSubset()));
        result->set_route_status(it->second->GetRecoverInfo());
      }
      if (have_data) {
        data->record_.set_rule_type(value->match_outbounds_ ? v1::RouteRecord::DestRule
                                                            : v1::RouteRecord::SrcRule);
      }
    }
    value->DecrementRef();
  }
  return data;
}

// 根据路由的Source匹配，查找匹配的路由
bool RuleServiceRouter::RouteMatch(ServiceRouteRule* route_rule, ServiceRouteRule* src_route_rule,
                                   ServiceInfo* source_service_info, RouteRuleBound*& matched_route,
                                   bool* match_outbounds, bool* have_route_rule,
                                   std::string& parameters) {
  // 优先匹配被调的入规则
  RouteRuleData* dst_rule_data          = reinterpret_cast<RouteRuleData*>(route_rule->RouteRule());
  std::vector<RouteRuleBound>& inbounds = dst_rule_data->inbounds_;
  *have_route_rule                      = inbounds.size() > 0;
  for (std::size_t i = 0; i < inbounds.size(); ++i) {
    if (inbounds[i].route_rule_.MatchSource(source_service_info, parameters)) {
      matched_route    = &inbounds[i];
      *match_outbounds = false;
      return true;
    }
  }
  if (inbounds.size() > 0) {  // 被调服务有入规则，但却没有匹配到路由
    return false;
  }
  // 被调没有入规则，如果传入了主调服务信息，则再匹配主调的出规则
  if (src_route_rule != NULL) {
    RouteRuleData* src_rule_data = reinterpret_cast<RouteRuleData*>(src_route_rule->RouteRule());
    std::vector<RouteRuleBound>& outbounds = src_rule_data->outbounds_;
    *have_route_rule                       = *have_route_rule || outbounds.size() > 0;
    for (std::size_t i = 0; i < outbounds.size(); ++i) {
      if (outbounds[i].route_rule_.MatchSource(source_service_info, parameters)) {
        matched_route    = &outbounds[i];
        *match_outbounds = true;
        return true;
      }
    }
    if (outbounds.size() > 0) {  // 主调服务有出规则，但却没有匹配到路由
      return false;
    }
  }
  // 被调无入规则，主调无出规则或者无需匹配
  return true;
}

}  // namespace polaris
