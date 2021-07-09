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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_LOCALITY_AWARE_LOCALITY_AWARE_H_
#define POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_LOCALITY_AWARE_LOCALITY_AWARE_H_

#include <stddef.h>
#include <map>

#include "cache/service_cache.h"
#include "logger.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "utils/time_clock.h"

#include "locality_aware_selector.h"

namespace polaris {

class Config;
class Context;

struct FeedbackInfo {
  uint64_t call_daley;
  uint64_t locality_aware_info;
  InstanceId instance_id;
};

struct LocalityAwareLbCacheKey {
  InstancesSet *prior_data_;
  bool operator<(const LocalityAwareLbCacheKey &rhs) const {
    return this->prior_data_ < rhs.prior_data_;
  }
};

class LocalityAwareLBCacheValue : public CacheValueBase {
public:
  LocalityAwareLBCacheValue(int64_t min_weight, InstancesSet *prior_date)
      : locality_aware_selector_(min_weight) {
    route_key_  = 0;
    sum_weight_ = 0;
    prior_date_ = prior_date;
    prior_date_->IncrementRef();
  }
  virtual ~LocalityAwareLBCacheValue() {
    if (prior_date_ != NULL) {
      prior_date_->DecrementRef();
      prior_date_ = NULL;
    }
    route_key_ = 0;
    instance_map_.clear();
    sum_weight_ = 0;
    weight_instances_.clear();
  }

public:
  struct WeightInstance {
    int weight_;
    Instance *instance_;

    bool operator<(const WeightInstance &rhs) const { return this->weight_ < rhs.weight_; }
  };

  InstancesSet *prior_date_;
  std::set<Instance *> half_open_instances_;
  uint32_t route_key_;
  LocalityAwareSelector locality_aware_selector_;
  std::map<InstanceId, Instance *> instance_map_;
  int sum_weight_;
  std::vector<WeightInstance> weight_instances_;
};

class LocalityAwareLoadBalancer : public LoadBalancer {
public:
  LocalityAwareLoadBalancer();
  virtual ~LocalityAwareLoadBalancer();
  virtual ReturnCode Init(Config *config, Context *context);
  virtual LoadBalanceType GetLoadBalanceType() { return kLoadBalanceTypeLocalityAware; }
  virtual ReturnCode ChooseInstance(ServiceInstances *service_instances, const Criteria &criteria,
                                    Instance *&next);
  ReturnCode Feedback(const FeedbackInfo &info);

private:
  uint64_t CalculateLocalityAwareInfo(uint32_t route_key, uint64_t begin_time_ms);
  uint32_t GetRouteKey(uint64_t locality_aware_info);
  uint64_t GetBeginTimeMs(uint64_t locality_aware_info);

private:
  Context *context_;
  sync::Mutex mutex_;
  uint32_t route_key_count_;
  uint64_t system_begin_time_;            // us
  uint32_t describe_interval_;            // ms
  sync::Atomic<uint64_t> describe_time_;  // us
  int64_t min_weight_;
  ServiceCache<LocalityAwareLbCacheKey> *cache_key_data_cache_;
  ServiceCache<uint32_t> *rout_key_data_cache_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_LOCALITY_AWARE_LOCALITY_AWARE_H_