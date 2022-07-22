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

#include "plugin/load_balancer/ringhash/l5_csthash.h"

#include <stdio.h>

#include <iosfwd>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "context/context_impl.h"
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

static void BuildHashRing(const std::vector<Instance*>& instances, const std::set<Instance*>& half_open_instances,
                          std::map<uint32_t, Instance*>& hash_ring, bool brpc_murmur_hash) {
  char node[256] = {0};
  int len;
  uint32_t hash;

  std::map<uint32_t, Instance*>::iterator postion;
  for (auto& instance : instances) {
    if (half_open_instances.count(instance) > 0) {  // 过滤半开节点
      continue;
    }
    // 计算该节点应该建立的虚拟节点数
    for (uint32_t index = 0; index < instance->GetWeight(); index++) {
      if (brpc_murmur_hash) {
        len = snprintf(node, sizeof(node) - 1, "%s:%u-%u", instance->GetHost().c_str(), instance->GetPort(), index);
        hash = Murmur3_32(node, len, 0);
      } else {
        len = snprintf(node, sizeof(node) - 1, "%s:%u:%u", instance->GetHost().c_str(), index, instance->GetPort());
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
    : context_(nullptr), data_cache_(nullptr), brpc_murmur_hash_(c_murmur_hash) {}

L5CstHashLoadBalancer::~L5CstHashLoadBalancer() {
  if (data_cache_ != nullptr) {
    data_cache_->SetClearHandler(0);
    data_cache_->DecrementRef();
  }
  context_ = nullptr;
}

LoadBalanceType L5CstHashLoadBalancer::GetLoadBalanceType() {
  if (brpc_murmur_hash_) return kLoadBalanceTypeCMurmurHash;
  return kLoadBalanceTypeL5CstHash;
}
ReturnCode L5CstHashLoadBalancer::Init(Config* /*config*/, Context* context) {
  data_cache_ = new ServiceCache<L5CstHashCacheKey, L5CstHashCacheValue>();
  context_ = context;
  context_->GetContextImpl()->RegisterCache(data_cache_);
  return kReturnOk;
}

ReturnCode L5CstHashLoadBalancer::ChooseInstance(ServiceInstances* service_instances, const Criteria& criteria,
                                                 Instance*& next) {
  // 获取所有实例
  next = nullptr;
  InstancesSet* instances_set = service_instances->GetAvailableInstances();
  L5CstHashCacheKey cache_key = {instances_set};
  L5CstHashCacheValue* lb_value = data_cache_->GetWithRcuTime(cache_key);
  if (lb_value == nullptr) {
    lb_value = data_cache_->CreateOrGet(cache_key, [&] {
      auto& instances = instances_set->GetInstances();
      L5CstHashCacheValue* new_lb_value = new L5CstHashCacheValue();
      new_lb_value->prior_date_ = instances_set;
      new_lb_value->prior_date_->IncrementRef();
      service_instances->GetHalfOpenInstances(new_lb_value->half_open_instances_);
      BuildHashRing(instances, new_lb_value->half_open_instances_, new_lb_value->hash_ring, brpc_murmur_hash_);
      if (new_lb_value->hash_ring.empty()) {  // 忽略半开状态构建hash环
        std::set<Instance*> empty_half_open;
        BuildHashRing(instances, empty_half_open, new_lb_value->hash_ring, brpc_murmur_hash_);
      }
      return new_lb_value;
    });
  }

  if (!criteria.ignore_half_open_) {
    service_instances->GetService()->TryChooseHalfOpenInstance(lb_value->half_open_instances_, next);
    if (next != nullptr) {
      return kReturnOk;
    }
  }

  if (lb_value->hash_ring.empty()) {
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
  auto position = lb_value->hash_ring.lower_bound(hash);
  if (POLARIS_UNLIKELY(lb_value->hash_ring.end() == position)) {
    position = lb_value->hash_ring.begin();
  }
  // 获取hash key原本指向的节点
  if (criteria.replicate_index_ <= 0) {
    next = position->second;
    return kReturnOk;
  }

  // 先判断是否能够去重获取副本节点，避免无效轮询hash环
  auto replicate_index = static_cast<std::size_t>(criteria.replicate_index_ % instances_set->GetInstances().size());
  if (replicate_index == 0) {
    next = position->second;
    return kReturnOk;
  }
  auto replicate_position = position;
  std::set<Instance*> replicate_instances;
  replicate_instances.insert(replicate_position->second);
  for (std::size_t i = 0; i < lb_value->hash_ring.size(); ++i) {
    if (POLARIS_UNLIKELY(++replicate_position == lb_value->hash_ring.end())) {
      replicate_position = lb_value->hash_ring.begin();
    }
    replicate_instances.insert(replicate_position->second);
    if (replicate_instances.size() > replicate_index) {
      next = replicate_position->second;
      return kReturnOk;
    }
  }

  next = position->second;
  return kReturnOk;
}

}  // namespace polaris
