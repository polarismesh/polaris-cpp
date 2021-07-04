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

#include "plugin/load_balancer/l5_csthash.h"

#include <stdio.h>

#include <iosfwd>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "context_internal.h"
#include "logger.h"
#include "model/model_impl.h"
#include "plugin/load_balancer/hash/murmur.h"
#include "polaris/context.h"
#include "utils/ip_utils.h"
#include "utils/utils.h"

namespace polaris {

class Config;

static inline bool InstanceCmp(Instance& a, Instance& b) {
  if (a.GetWeight() > b.GetWeight()) {
    return true;
  } else if (a.GetWeight() == b.GetWeight()) {
    uint32_t a_ip = 0;
    uint32_t b_ip = 0;
    IpUtils::StrIpToInt(a.GetHost(), a_ip);
    IpUtils::StrIpToInt(b.GetHost(), b_ip);
    return a_ip < b_ip ? true : (a_ip == b_ip ? (a.GetPort() < b.GetPort()) : false);
  } else {
    return false;
  }
}

static void BuildHashRing(std::vector<Instance*>& instances,
                          std::map<uint32_t, Instance*>& hash_ring, bool brpc_murmur_hash) {
  char node[256] = {0};
  int len;
  uint32_t hash;

  std::map<uint32_t, Instance*>::iterator postion;
  for (std::size_t i = 0; i < instances.size(); ++i) {
    Instance*& instance = instances[i];
    // 计算该节点应该建立的虚拟节点数
    for (uint32_t index = 0; index < instance->GetWeight(); index++) {
      if (brpc_murmur_hash) {
        len  = snprintf(node, sizeof(node) - 1, "%s:%u-%u", instance->GetHost().c_str(),
                       instance->GetPort(), index);
        hash = Murmur3_32(node, len, 0);
      } else {
        len  = snprintf(node, sizeof(node) - 1, "%s:%u:%u", instance->GetHost().c_str(), index,
                       instance->GetPort());
        hash = Murmur3_32(node, len, 16);
      }
      postion = hash_ring.find(hash);
      if (hash_ring.end() != postion) {
        if (!InstanceCmp(*postion->second, *instance)) {  // hash冲突后选用weight大的
          postion->second = instance;
        }
        continue;
      }
      hash_ring.insert(std::make_pair(hash, instance));
    }
  }
}

L5CstHashLoadBalancer::L5CstHashLoadBalancer(bool c_murmur_hash)
    : context_(NULL), data_cache_(NULL), brpc_murmur_hash_(c_murmur_hash) {}

L5CstHashLoadBalancer::~L5CstHashLoadBalancer() {
  if (data_cache_ != NULL) {
    data_cache_->SetClearHandler(0);
    data_cache_->DecrementRef();
  }
  context_ = NULL;
}

LoadBalanceType L5CstHashLoadBalancer::GetLoadBalanceType() {
  if (brpc_murmur_hash_) return kLoadBalanceTypeCMurmurHash;
  return kLoadBalanceTypeL5CstHash;
}
ReturnCode L5CstHashLoadBalancer::Init(Config* /*config*/, Context* context) {
  data_cache_ = new ServiceCache<L5CstHashCacheKey>();
  context_    = context;
  context_->GetContextImpl()->RegisterCache(data_cache_);
  return kReturnOk;
}

ReturnCode L5CstHashLoadBalancer::ChooseInstance(ServiceInstances* service_instances,
                                                 const Criteria& criteria, Instance*& next) {
  // 获取所有实例
  next                        = NULL;
  InstancesSet* instances_set = service_instances->GetAvailableInstances();
  L5CstHashCacheKey cache_key = {instances_set};
  ServiceBase* cache_value    = data_cache_->GetWithRef(cache_key);
  L5CstHashCacheValue* lb_value;
  if (cache_value != NULL) {
    lb_value = dynamic_cast<L5CstHashCacheValue*>(cache_value);
    POLARIS_ASSERT(lb_value != NULL);
  } else {
    std::vector<Instance*> instances = instances_set->GetInstances();
    lb_value                         = new L5CstHashCacheValue();
    lb_value->prior_date_            = instances_set;
    lb_value->prior_date_->IncrementRef();
    service_instances->GetHalfOpenInstances(lb_value->half_open_instances_);
    BuildHashRing(instances, lb_value->hash_ring, brpc_murmur_hash_);
    data_cache_->PutWithRef(cache_key, lb_value);
  }

  if (!criteria.ignore_half_open_) {
    service_instances->GetService()->TryChooseHalfOpenInstance(lb_value->half_open_instances_,
                                                               next);
    if (next != NULL) {
      lb_value->DecrementRef();
      return kReturnOk;
    }
  }

  if (lb_value->hash_ring.empty()) {
    lb_value->DecrementRef();
    return kReturnInstanceNotFound;
  }

  uint32_t hash = 0;
  if (brpc_murmur_hash_) {
    if (criteria.hash_key_ != 0 || criteria.hash_string_.empty()) {
      hash = criteria.hash_key_;
    } else {
      hash = Murmur3_32(criteria.hash_string_.data(), criteria.hash_string_.size(), 0);
    }
  } else {
    hash = Murmur3_32((const char*)&criteria.hash_key_, sizeof(criteria.hash_key_), 16);
  }
  std::map<uint32_t, Instance*>::iterator position = lb_value->hash_ring.lower_bound(hash);
  for (int i = 0; i < criteria.replicate_index_; ++i) {
    if (POLARIS_UNLIKELY(lb_value->hash_ring.end() == position)) {
      position = lb_value->hash_ring.begin();
    }
    position++;
  }
  if (POLARIS_LIKELY(lb_value->hash_ring.end() != position)) {
    next = position->second;
  } else {
    next = lb_value->hash_ring.begin()->second;
  }
  lb_value->DecrementRef();
  return kReturnOk;
}

}  // namespace polaris
