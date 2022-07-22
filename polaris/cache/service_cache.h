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

#ifndef POLARIS_CPP_POLARIS_CACHE_SERVICE_CACHE_H_
#define POLARIS_CPP_POLARIS_CACHE_SERVICE_CACHE_H_

#include <stdint.h>

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "cache/rcu_map.h"
#include "model/model_impl.h"
#include "plugin/service_router/service_router.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "reactor/task.h"
#include "utils/string_utils.h"

namespace polaris {

class InstancesSet;

class RouterSubsetCache : public ServiceBase {
 public:
  RouterSubsetCache();
  virtual ~RouterSubsetCache();

 public:
  ServiceData* instances_data_;  // 保证原始服务实例不被释放
  InstancesSet* current_data_;
};

///////////////////////////////////////////////////////////////////////////////
// 规则路由缓存Key

struct RouteRuleBound;

struct RuleRouteCacheKey {
  InstancesSet* prior_data_;
  RouteRuleBound* route_key_;  // 路由规则指针
  uint64_t circuit_breaker_version_;
  uint64_t subset_circuit_breaker_version_;  // subset熔断版本号
  std::string labels_;                       //熔断接口标记
  uint8_t request_flags_;
  std::string parameters_;  // 参数接口

  bool operator<(const RuleRouteCacheKey& rhs) const {
    if (this->route_key_ < rhs.route_key_) {
      return true;
    } else if (this->route_key_ > rhs.route_key_) {
      return false;
    } else if (this->prior_data_ < rhs.prior_data_) {
      return true;
    } else if (this->prior_data_ > rhs.prior_data_) {
      return false;
    } else if (this->circuit_breaker_version_ < rhs.circuit_breaker_version_) {
      return true;
    } else if (this->circuit_breaker_version_ > rhs.circuit_breaker_version_) {
      return false;
    } else if (this->labels_.compare(rhs.labels_) < 0) {
      return true;
    } else if (this->labels_.compare(rhs.labels_) > 0) {
      return false;
    } else if (this->request_flags_ < rhs.request_flags_) {
      return true;
    } else if (this->request_flags_ > rhs.request_flags_) {
      return false;
    } else if (this->subset_circuit_breaker_version_ < rhs.subset_circuit_breaker_version_) {
      return true;
    } else if (this->subset_circuit_breaker_version_ > rhs.subset_circuit_breaker_version_) {
      return false;
    } else {
      return this->parameters_ < rhs.parameters_;
    }
  }
};

// 规则路由缓存Value
class RuleRouterCacheValue : public ServiceBase {
 public:
  RuleRouterCacheValue();

  virtual ~RuleRouterCacheValue();

 public:
  ServiceData* instances_data_;                // 保证原始服务实例不被释放
  ServiceData* route_rule_;                    // 保证原始服务路由不被释放
  std::map<uint32_t, InstancesSet*> subsets_;  // 匹配到的subset
  uint32_t subset_sum_weight_;
  bool match_outbounds_;
  bool is_redirect_;  // 是否为服务转发
  ServiceKey redirect_service_;
};

///////////////////////////////////////////////////////////////////////////////
// 就近路由缓存Key
struct NearbyCacheKey {
  InstancesSet* prior_data_;
  uint64_t circuit_breaker_version_;
  uint32_t location_version_;
  uint8_t request_flags_;

  bool operator<(const NearbyCacheKey& rhs) const {
    if (this->prior_data_ < rhs.prior_data_) {
      return true;
    } else if (this->prior_data_ > rhs.prior_data_) {
      return false;
    } else if (this->location_version_ < rhs.location_version_) {
      return true;
    } else if (this->location_version_ > rhs.location_version_) {
      return false;
    } else if (this->request_flags_ < rhs.request_flags_) {
      return true;
    } else if (this->request_flags_ > rhs.request_flags_) {
      return false;
    } else {
      return this->circuit_breaker_version_ < rhs.circuit_breaker_version_;
    }
  }
};

///////////////////////////////////////////////////////////////////////////////
// 分SET路由缓存Key
struct SetDivisionCacheKey {
  InstancesSet* prior_data_;
  std::string caller_set_name;
  uint64_t circuit_breaker_version_;
  uint8_t request_flags_;

  bool operator<(const SetDivisionCacheKey& rhs) const {
    if (this->prior_data_ < rhs.prior_data_) {
      return true;
    } else if (this->prior_data_ > rhs.prior_data_) {
      return false;
    } else if (this->caller_set_name < rhs.caller_set_name) {
      return true;
    } else if (this->caller_set_name > rhs.caller_set_name) {
      return false;
    } else if (this->circuit_breaker_version_ < rhs.circuit_breaker_version_) {
      return true;
    } else if (this->circuit_breaker_version_ > rhs.circuit_breaker_version_) {
      return false;
    } else {
      return this->request_flags_ < rhs.request_flags_;
    }
  }
};

// 分SET路由缓存Value
class SetDivisionCacheValue : public RouterSubsetCache {
 public:
  SetDivisionCacheValue() : enable_set(false) {}

 public:
  bool enable_set;
};

///////////////////////////////////////////////////////////////////////////////
// 金丝雀路由缓存Key
struct CanaryCacheKey {
  InstancesSet* prior_data_;
  uint64_t circuit_breaker_version_;
  std::string canary_value_;

  bool operator<(const CanaryCacheKey& rhs) const {
    if (this->prior_data_ < rhs.prior_data_) {
      return true;
    } else if (this->prior_data_ > rhs.prior_data_) {
      return false;
    } else if (this->circuit_breaker_version_ < rhs.circuit_breaker_version_) {
      return true;
    } else if (this->circuit_breaker_version_ > rhs.circuit_breaker_version_) {
      return false;
    } else {
      return this->canary_value_ < rhs.canary_value_;
    }
  }
};

///////////////////////////////////////////////////////////////////////////////
// 元数据路由缓存Key
struct MetadataCacheKey {
  InstancesSet* prior_data_;
  uint64_t circuit_breaker_version_;
  std::map<std::string, std::string> metadata_;
  MetadataFailoverType failover_type_;

  bool operator<(const MetadataCacheKey& rhs) const {
    if (this->prior_data_ < rhs.prior_data_) {
      return true;
    } else if (this->prior_data_ > rhs.prior_data_) {
      return false;
    } else if (this->circuit_breaker_version_ < rhs.circuit_breaker_version_) {
      return true;
    } else if (this->circuit_breaker_version_ > rhs.circuit_breaker_version_) {
      return false;
    } else if (this->failover_type_ < rhs.failover_type_) {
      return true;
    } else if (this->failover_type_ > rhs.failover_type_) {
      return false;
    } else {
      return this->metadata_ < rhs.metadata_;
    }
  }
};

///////////////////////////////////////////////////////////////////////////////
class Clearable : public ServiceBase {
 public:
  Clearable() { clear_handler_ = 0; }
  virtual ~Clearable() {}

  virtual void Clear(uint64_t min_access_epoch) = 0;

  void SetClearHandler(uint64_t clear_handler) { clear_handler_ = clear_handler; }
  uint64_t GetClearHandler() { return clear_handler_; }

 private:
  uint64_t clear_handler_;
};

///////////////////////////////////////////////////////////////////////////////
// 服务数据缓存
template <typename Key, typename Value>
class ServiceCache : public Clearable {
 public:
  ServiceCache() {}

  virtual ~ServiceCache() {}

  Value* CreateOrGet(const Key& key, std::function<Value*()> creator) {
    return buffered_cache_.CreateOrGet(key, creator);
  }

  Value* GetWithRcuTime(const Key& key) { return buffered_cache_.GetWithRcuTime(key); }

  virtual void Clear(uint64_t min_access_time) {
    typename std::vector<Key> clear_keys;
    buffered_cache_.CheckExpired(min_access_time, clear_keys);
    for (std::size_t i = 0; i < clear_keys.size(); ++i) {
      buffered_cache_.Delete(clear_keys[i]);
    }
    buffered_cache_.CheckGc(min_access_time);
  }

  void GetAllValuesWithRef(std::vector<Value*>& values) { buffered_cache_.GetAllValuesWithRef(values); }

  RouterStatData* CollectStat() {
    RouterStatData* data = nullptr;
    std::vector<Value*> values;
    buffered_cache_.GetAllValuesWithRef(values);
    for (std::size_t i = 0; i < values.size(); ++i) {
      Value* value = values[i];
      int count = value->current_data_->GetImpl()->count_.exchange(0);
      if (count != 0) {
        if (data == nullptr) {
          data = new RouterStatData();
        }
        v1::RouteResult* result = data->record_.add_results();
        result->set_ret_code("Success");
        result->set_period_times(count);
        result->set_cluster(StringUtils::MapToStr(value->current_data_->GetSubset()));
        result->set_route_status(value->current_data_->GetRecoverInfo());
      }
      value->DecrementRef();
    }
    return data;
  }

 private:
  RcuMap<Key, Value> buffered_cache_;
};

///////////////////////////////////////////////////////////////////////////////
class ServiceCacheUpdateParam {
 public:
  ServiceInfo source_service_info_;
  uint8_t request_flag_;
  std::map<std::string, std::string> labels_;
  MetadataRouterParam metadata_param_;

  ServiceInfo* GetSourceServiceInfo() {
    if (source_service_info_.metadata_.empty() && source_service_info_.service_key_.name_.empty() &&
        source_service_info_.service_key_.namespace_.empty()) {
      return nullptr;
    }
    return &source_service_info_;
  }

  bool operator<(const ServiceCacheUpdateParam& rhs) const {
    if (source_service_info_.metadata_ < rhs.source_service_info_.metadata_) {
      return true;
    } else if (source_service_info_.metadata_ > rhs.source_service_info_.metadata_) {
      return false;
    } else if (request_flag_ < rhs.request_flag_) {
      return true;
    } else if (request_flag_ > rhs.request_flag_) {
      return false;
    } else if (metadata_param_.failover_type_ < rhs.metadata_param_.failover_type_) {
      return true;
    } else if (metadata_param_.failover_type_ > rhs.metadata_param_.failover_type_) {
      return false;
    } else if (metadata_param_.metadata_ < rhs.metadata_param_.metadata_) {
      return true;
    } else if (metadata_param_.metadata_ > rhs.metadata_param_.metadata_) {
      return false;
    } else if (labels_ < rhs.labels_) {
      return true;
    } else if (labels_ > rhs.labels_) {
      return false;
    } else {
      return source_service_info_.service_key_ < rhs.source_service_info_.service_key_;
    }
  }
};

class ContextImpl;
class ServiceCacheUpdateTask : public Task {
 public:
  ServiceCacheUpdateTask(const ServiceKey& service_key, uint64_t circuit_breaker_version, ContextImpl* context_impl)
      : service_key_(service_key), circuit_breaker_version_(circuit_breaker_version), context_impl_(context_impl) {}

  virtual ~ServiceCacheUpdateTask() {}

  virtual void Run();

 private:
  ServiceKey service_key_;
  uint64_t circuit_breaker_version_;
  ContextImpl* context_impl_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CACHE_SERVICE_CACHE_H_
