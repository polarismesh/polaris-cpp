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

#include "plugin/load_balancer/ringhash/continuum.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <utility>

#include "logger.h"
#include "model/instance.h"
#include "model/model_impl.h"
#include "polaris/model.h"
#include "utils/utils.h"

namespace polaris {

// hash 冲突情况下最多尝试次数
static const int kMaxRehashIteration = 5;

ContinuumSelector::ContinuumSelector(Hash64Func hash_func) : hash_func_(hash_func) {}

ContinuumSelector::~ContinuumSelector() {}

void ContinuumSelector::Setup(const std::vector<Instance*>& instances, const std::set<Instance*>& half_open_instances,
                              uint32_t vnode_cnt, int base_weight, bool dynamic_weight) {
  std::size_t count = instances.size();
  ring_.clear();
  ring_.reserve(count * vnode_cnt);

  // 如果配置了基础权重，则以基础权重计算虚拟节点数， 否则以最大权重为基准计算虚拟节点数
  double max_weight = base_weight > 0 ? static_cast<double>(base_weight)
                                      : static_cast<double>(InstancesSetImpl::CalcMaxWeight(instances));
  char buff[128] = {0};
  uint64_t hash_value = 0;
  std::map<uint64_t, std::string>::iterator hash_it;
  std::map<uint64_t, std::string> hash_value_key;

  for (std::size_t i = 0; i < count; ++i) {
    Instance* inst = instances[i];
    if (half_open_instances.count(inst) > 0) {
      continue;
    }
    uint32_t instance_weight = dynamic_weight ? inst->GetDynamicWeight() : inst->GetWeight();
    int limit = static_cast<int>(floor(instance_weight * vnode_cnt / max_weight));
    for (int k = 0; k < limit; ++k) {
      memset(buff, 0, sizeof(buff));
      snprintf(buff, sizeof(buff), "%s%d", inst->GetId().c_str(), k);
      hash_value = hash_func_(static_cast<const void*>(buff), strlen(buff), 0);
      hash_it = hash_value_key.find(hash_value);
      if (POLARIS_LIKELY(hash_it == hash_value_key.end())) {
        hash_value_key[hash_value] = buff;
        ring_.push_back(ContinuumPoint(hash_value, i));
        continue;
      }
      // conflict
      POLARIS_LOG(LOG_WARN, "hash=%" PRId64 " conflict between %s and %s", hash_value, hash_it->second.c_str(), buff);
      if (ReHash(1, hash_value, hash_value_key)) {
        ring_.push_back(ContinuumPoint(hash_value, i));
      } else {
        POLARIS_LOG(LOG_ERROR, "fail to generate hash @ %s:%u(id=%s limit=%d). reach %d tries", inst->GetHost().c_str(),
                    inst->GetPort(), inst->GetId().c_str(), k, kMaxRehashIteration);
      }
    }
  }
  std::sort(ring_.begin(), ring_.end());
}

void ContinuumSelector::FastSetup(const std::vector<Instance*>& instances,
                                  const std::set<Instance*>& half_open_instances, uint32_t vnode_cnt, int base_weight,
                                  bool dynamic_weight) {
  std::size_t count = instances.size();
  ring_.clear();
  ring_.reserve(count * vnode_cnt);

  std::unordered_map<uint64_t, HashKeyIndex> hash_val_to_key;
  std::unordered_map<uint64_t, HashKeyIndex>::iterator hash_it;

  char buff[128];
  // 如果配置了基础权重，则以基础权重计算虚拟节点数，否则以平均权重为基准使用配置的虚拟节点数
  double avg_weight = base_weight > 0 ? static_cast<double>(base_weight)
                                      : static_cast<double>(InstancesSetImpl::CalcTotalWeight(instances)) / count;
  ContinuumPoint cp(0, 0);
  HashKeyIndex hash_key_index;
  for (size_t i = 0; i < count; ++i) {
    Instance* inst = instances[i];
    if (half_open_instances.count(inst) > 0) {
      continue;
    }
    hash_key_index.instance_index_ = i;
    hash_key_index.vnode_index_ = 0;
    hash_val_to_key[instances[i]->GetHash()] = hash_key_index;

    auto& local_value = inst->GetImpl().GetLocalValue();
    std::vector<uint64_t>& vnodeHash = local_value->AcquireVnodeHash();
    uint32_t instance_weight = dynamic_weight ? inst->GetDynamicWeight() : inst->GetWeight();
    int limit = static_cast<int>(floor(instance_weight * vnode_cnt / avg_weight)) - 1;
    cp.hashVal = inst->GetHash();
    cp.index = i;
    ring_.push_back(cp);  // 添加真实节点

    int hashCnt = vnodeHash.size();
    uint64_t hashVal;
    for (int k = 0, j = hashCnt + 1; k < limit; ++k) {
      int retry = 1;
      do {
        if (POLARIS_LIKELY(k < hashCnt && 1 == retry)) {  // 不需要计算哈希值
          hashVal = vnodeHash[k];
        } else {
          memset(buff, 0, sizeof(buff));
          snprintf(buff, sizeof(buff), "%s:%d", inst->GetId().c_str(), j++);
          hashVal = hash_func_(static_cast<const void*>(buff), strlen(buff), 0);
        }
        hash_it = hash_val_to_key.find(hashVal);
        if (POLARIS_LIKELY(hash_it == hash_val_to_key.end())) {
          hash_key_index.vnode_index_ = j;
          hash_val_to_key[hashVal] = hash_key_index;
          cp.hashVal = hashVal;
          ring_.push_back(cp);
          if (k >= hashCnt) {  // 哈希值不足, 添加进去
            vnodeHash.push_back(hashVal);
          } else if (retry > 1) {  // 哈希有冲突, 更新一下
            vnodeHash[k] = hashVal;
          }
          break;
        }
        // conflict
        POLARIS_LOG(LOG_WARN, "hash conflict between %s:%d and %s",
                    instances[hash_it->second.instance_index_]->GetId().c_str(), hash_it->second.vnode_index_, buff);
      } while (++retry <= kMaxRehashIteration);
      if (retry > kMaxRehashIteration) {
        POLARIS_LOG(LOG_ERROR, "fail to generate hash @ %s:%u(id=%s limit=%d). reach %d tries", inst->GetHost().c_str(),
                    inst->GetPort(), inst->GetId().c_str(), k, kMaxRehashIteration);
      }
    }

    if ((vnode_cnt * 2) >= static_cast<uint32_t>(limit * 3)) {  // 记录的 hash 值大于需要的 1.5 倍
      vnodeHash.resize(limit);
    }
    local_value->ReleaseVnodeHash();
  }
  std::sort(ring_.begin(), ring_.end());
}

int ContinuumSelector::Select(const Criteria& criteria) {
  if (ring_.empty()) {
    return -1;
  }
  uint64_t hash_value = CalculateHashValue(criteria);
  auto position = std::lower_bound(ring_.begin(), ring_.end(), hash_value);
  if (POLARIS_LIKELY(ring_.end() == position)) {
    position = ring_.begin();
  }
  return position->index;
}

uint64_t ContinuumSelector::CalculateHashValue(const Criteria& criteria) const {
  if (!criteria.hash_string_.empty()) {
    const std::string& hash_key = criteria.hash_string_;
    return hash_func_(static_cast<const void*>(hash_key.c_str()), hash_key.size(), 0);
  }
  if (POLARIS_LIKELY(criteria.hash_key_ != 0)) {
    return hash_func_(static_cast<const void*>(&criteria.hash_key_), sizeof(uint64_t), 0);
  }
  char buff[128];
  memset(buff, 0, sizeof(buff));
  snprintf(buff, sizeof(buff), "ringhash-%ld-%d", time(nullptr), rand());
  return hash_func_(static_cast<const void*>(buff), strlen(buff), 0);
}

ReturnCode ContinuumSelector::SelectReplicate(const std::vector<Instance*>& instances, const Criteria& criteria,
                                              Instance*& next) {
  if (ring_.empty()) {
    return kReturnInstanceNotFound;
  }
  uint64_t hash_value = CalculateHashValue(criteria);
  auto position = std::lower_bound(ring_.begin(), ring_.end(), hash_value);
  if (position == ring_.end()) {
    position = ring_.begin();
  }

  // 避免副本索引超出总实例大小
  std::size_t replicate_index = static_cast<std::size_t>(criteria.replicate_index_ % instances.size());
  if (POLARIS_UNLIKELY(replicate_index == 0)) {  // 获取原始节点
    next = instances[position->index];
    return kReturnOk;
  }
  // 有足够实例去重获取副本节点，查找副本节点并去重
  auto replicate_position = position;
  std::set<Instance*> replicate_instances;
  replicate_instances.insert(instances[replicate_position->index]);
  for (std::size_t i = 0; i < ring_.size(); ++i) {
    if (POLARIS_UNLIKELY(++replicate_position == ring_.end())) {
      replicate_position = ring_.begin();
    }
    auto& instance = instances[replicate_position->index];
    replicate_instances.insert(instance);
    if (replicate_instances.size() > replicate_index) {
      next = instance;
      return kReturnOk;
    }
  }

  next = instances[position->index];
  return kReturnOk;
}

bool ContinuumSelector::ReHash(int iteration, uint64_t& hash_value, std::map<uint64_t, std::string>& hash_value_key) {
  if (iteration > kMaxRehashIteration) {
    return false;
  }
  std::string hash_str = std::to_string(hash_value);
  hash_value = hash_func_(static_cast<const void*>(hash_str.c_str()), hash_str.size(), 0);
  std::map<uint64_t, std::string>::iterator hash_it = hash_value_key.find(hash_value);
  if (POLARIS_LIKELY(hash_it == hash_value_key.end())) {
    hash_value_key[hash_value] = hash_str;
    return true;
  }
  return ReHash(iteration + 1, hash_value, hash_value_key);
}

}  // namespace polaris