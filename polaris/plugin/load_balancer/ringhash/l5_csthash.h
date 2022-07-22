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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_RINGHASH_L5_CSTHASH_H_
#define POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_RINGHASH_L5_CSTHASH_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <set>

#include "cache/service_cache.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "polaris/plugin.h"

namespace polaris {

class Config;
class Context;

struct L5CstHashCacheKey {
  InstancesSet* prior_data_;

  bool operator<(const L5CstHashCacheKey& rhs) const { return this->prior_data_ < rhs.prior_data_; }
};

class L5CstHashCacheValue : public ServiceBase {
 public:
  virtual ~L5CstHashCacheValue() {
    prior_date_->DecrementRef();
    prior_date_ = nullptr;
  }

 public:
  InstancesSet* prior_date_;
  std::map<uint32_t, Instance*> hash_ring;
  std::set<Instance*> half_open_instances_;
};

// 兼容L5的一致性hash算法，相同数据提供与L5相同的输出
class L5CstHashLoadBalancer : public LoadBalancer {
 public:
  explicit L5CstHashLoadBalancer(bool c_murmur_hash = false);

  virtual ~L5CstHashLoadBalancer();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual LoadBalanceType GetLoadBalanceType();

  virtual ReturnCode ChooseInstance(ServiceInstances* instances, const Criteria& criteria, Instance*& next);

 private:
  Context* context_;
  ServiceCache<L5CstHashCacheKey, L5CstHashCacheValue>* data_cache_;
  bool brpc_murmur_hash_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_RINGHASH_L5_CSTHASH_H_
