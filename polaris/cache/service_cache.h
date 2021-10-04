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
#include "polaris/defs.h"
#include "polaris/model.h"
#include "utils/string_utils.h"

namespace polaris {

class InstancesSet;

typedef ServiceBase CacheValueBase;

class RouterSubsetCache : public CacheValueBase {
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
class RuleRouterCacheValue : public CacheValueBase {
public:
  RuleRouterCacheValue();

  virtual ~RuleRouterCacheValue();

public:
  ServiceData* instances_data_;  // 保证原始服务实例不被释放
  ServiceData* route_rule_;      // 保证原始服务路由不被释放
  std::map<uint32_t, InstancesSet*> data_;
  uint32_t sum_weight_;
  bool match_outbounds_;
};

///////////////////////////////////////////////////////////////////////////////
// 就近路由缓存Key
struct NearbyCacheKey {
  InstancesSet* prior_data_;
  uint64_t location_version_;
  uint64_t circuit_breaker_version_;
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
    } else if (this->request_flags_ < rhs.request_flags_) {
      return true;
    } else if (this->request_flags_ > rhs.request_flags_) {
      return false;
    } else {
      return false;
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
template <typename K>
class ServiceCache : public Clearable {
public:
  ServiceCache() {}

  virtual ~ServiceCache() {}

  void PutWithRef(const K& key, CacheValueBase* cache_value) {
    cache_value->IncrementRef();
    buffered_cache_.Update(key, cache_value);
  }

  CacheValueBase* GetWithRef(const K& key) { return buffered_cache_.Get(key); }

  virtual void Clear(uint64_t min_access_time) {
    typename std::vector<K> clear_keys;
    buffered_cache_.CheckExpired(min_access_time, clear_keys);
    for (std::size_t i = 0; i < clear_keys.size(); ++i) {
      buffered_cache_.Delete(clear_keys[i]);
    }
    buffered_cache_.CheckGc(min_access_time);
  }

  void GetAllValuesWithRef(std::vector<CacheValueBase*>& values) {
    buffered_cache_.GetAllValuesWithRef(values);
  }

  RouterStatData* CollectStat() {
    RouterStatData* data = NULL;
    std::vector<CacheValueBase*> values;
    buffered_cache_.GetAllValuesWithRef(values);
    for (std::size_t i = 0; i < values.size(); ++i) {
      RouterSubsetCache* value = dynamic_cast<RouterSubsetCache*>(values[i]);
      POLARIS_ASSERT(value != NULL);
      int count = value->current_data_->GetInstancesSetImpl()->count_.Exchange(0);
      if (count != 0) {
        if (data == NULL) {
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
  RcuMap<K, CacheValueBase> buffered_cache_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CACHE_SERVICE_CACHE_H_
