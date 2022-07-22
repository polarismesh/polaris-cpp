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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_WEIGHTED_RANDOM_H_
#define POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_WEIGHTED_RANDOM_H_

#include <stddef.h>
#include <set>
#include <vector>

#include "cache/service_cache.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "polaris/plugin.h"

namespace polaris {

struct RandomLbCacheKey {
  InstancesSet* prior_data_;
  uint64_t version_;

  bool operator<(const RandomLbCacheKey& rhs) const {
    return this->prior_data_ < rhs.prior_data_ ||
           (this->prior_data_ == rhs.prior_data_ && this->version_ < rhs.version_);
  }
};

struct WeightInstance {
  int weight_;
  Instance* instance_;

  bool operator<(const WeightInstance& rhs) const { return this->weight_ < rhs.weight_; }
};

class RandomLbCacheValue : public ServiceBase {
 public:
  virtual ~RandomLbCacheValue() {
    prior_date_->DecrementRef();
    prior_date_ = nullptr;
    sum_weight_ = 0;
    weight_instances_.clear();
  }

 public:
  InstancesSet* prior_date_;
  std::set<Instance*> half_open_instances_;
  int sum_weight_;
  std::vector<WeightInstance> weight_instances_;
};

class RandomLoadBalancer : public LoadBalancer {
 public:
  RandomLoadBalancer();

  virtual ~RandomLoadBalancer();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual RandomLbCacheKey GenCacheKey(ServiceInstances* service_instances);

  virtual LoadBalanceType GetLoadBalanceType() { return kLoadBalanceTypeWeightedRandom; }

  virtual ReturnCode ChooseInstance(ServiceInstances* instances, const Criteria& criteria, Instance*& next);

 protected:
  bool enable_dynamic_weight_;
  Context* context_;

 private:
  ServiceCache<RandomLbCacheKey, RandomLbCacheValue>* data_cache_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_WEIGHTED_RANDOM_H_
