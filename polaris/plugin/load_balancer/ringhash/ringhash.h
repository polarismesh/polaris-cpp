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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_RINGHASH_RINGHASH_H_
#define POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_RINGHASH_RINGHASH_H_

#include <stdint.h>

#include "cache/service_cache.h"
#include "plugin/load_balancer/hash/hash_manager.h"
#include "plugin/load_balancer/ringhash/continuum.h"
#include "polaris/defs.h"
#include "polaris/plugin.h"

namespace polaris {

class InstancesData;

struct RingHashCacheKey {
  InstancesSet* prior_data_;
  uint64_t version_;

  bool operator<(const RingHashCacheKey& rhs) const {
    return this->prior_data_ < rhs.prior_data_ ||
           (this->prior_data_ == rhs.prior_data_ && this->version_ < rhs.version_);
  }
};

class RingHashCacheValue : public ServiceBase {
 public:
  virtual ~RingHashCacheValue() {
    prior_date_->DecrementRef();
    prior_date_ = nullptr;
  }

 public:
  InstancesSet* prior_date_;
  std::unique_ptr<ContinuumSelector> selector_;
  std::set<Instance*> half_open_instances_;
};

class KetamaLoadBalancer : public LoadBalancer {
 public:
  KetamaLoadBalancer();

  virtual ~KetamaLoadBalancer();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual LoadBalanceType GetLoadBalanceType() { return kLoadBalanceTypeRingHash; }

  virtual ReturnCode ChooseInstance(ServiceInstances* service_instances, const Criteria& criteria, Instance*& next);

  static void OnInstanceUpdate(const InstancesData* old, InstancesData* new_instances);

 private:
  Context* context_;
  uint32_t vnode_cnt_;
  int base_weight_;
  Hash64Func hash_func_;
  bool compatible_go_;  // 兼容golang sdk的一致性hash算法

  ServiceCache<RingHashCacheKey, RingHashCacheValue>* data_cache_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_RINGHASH_RINGHASH_H_
