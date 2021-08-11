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

#if __cplusplus >= 201103L
#include <unordered_map>
#else
#include <map>
#endif
#include <algorithm>
#include <memory>
#include <utility>

#include "logger.h"
#include "model/model_impl.h"
#include "polaris/model.h"
#include "utils/string_utils.h"
#include "utils/utils.h"

namespace polaris {

// hash 冲突情况下最多尝试次数
static const int kMaxRehashIteration = 5;

ContinuumSelector::ContinuumSelector() : hashFunc_(NULL), ringLen_(0) {}

ContinuumSelector::~ContinuumSelector() {
  hashFunc_ = NULL;
  ring_.clear();
  ringLen_ = 0;
}

bool ContinuumSelector::Setup(InstancesSet* instance_set, uint32_t vnode_cnt,
                              Hash64Func hash_func) {
  if (NULL == instance_set || NULL == hash_func || 0 == vnode_cnt) {
    POLARIS_LOG(LOG_ERROR, "Invalid parameters. instanceSet/hashFunc is NULL, or vnodeCnt is zero");
    return false;
  }

  const std::vector<Instance*>& instances = instance_set->GetInstances();
  std::size_t count                       = instances.size();
  if (0 == count) {
    POLARIS_LOG(LOG_ERROR, "No available instances");
    return false;
  }
  hashFunc_ = hash_func;
  ring_.clear();
  ringLen_ = count * vnode_cnt;
  ring_.reserve(ringLen_);

  double max_weight   = static_cast<double>(CalcMaxWeight(instances));
  double percent      = 0;
  char buff[128]      = {0};
  uint64_t hash_value = 0;
  std::map<uint64_t, std::string>::iterator hash_it;
  std::map<uint64_t, std::string> hash_value_key;

  for (std::size_t i = 0; i < count; ++i) {
    Instance* inst = instances[i];
    percent        = static_cast<double>(inst->GetWeight()) / max_weight;
    int limit      = static_cast<int>(floor(percent * vnode_cnt));
    for (int k = 0; k < limit; ++k) {
      memset(buff, 0, sizeof(buff));
      snprintf(buff, sizeof(buff), "%s%d", inst->GetId().c_str(), k);
      hash_value = hashFunc_(static_cast<const void*>(buff), strlen(buff), 0);
      hash_it    = hash_value_key.find(hash_value);
      if (POLARIS_LIKELY(hash_it == hash_value_key.end())) {
        hash_value_key[hash_value] = buff;
        ring_.push_back(ContinuumPoint(hash_value, i));
        continue;
      }
      // conflict
      POLARIS_LOG(LOG_WARN, "hash=%" PRId64 " conflict between %s and %s", hash_value,
                  hash_it->second.c_str(), buff);
      if (ReHash(1, hash_value, hash_value_key)) {
        ring_.push_back(ContinuumPoint(hash_value, i));
      } else {
        POLARIS_LOG(LOG_ERROR, "fail to generate hash @ %s:%u(id=%s limit=%d). reach %d tries",
                    inst->GetHost().c_str(), inst->GetPort(), inst->GetId().c_str(), k,
                    kMaxRehashIteration);
      }
    }
  }

  ringLen_ = ring_.size();
  if (ring_.size() > 1) {
    std::sort(ring_.begin(), ring_.end());
  }
  return true;
}

bool ContinuumSelector::FastSetup(InstancesSet* instanceSet, uint32_t vnodeCnt,
                                  Hash64Func hashFunc) {
  if (NULL == instanceSet || NULL == hashFunc || 0 == vnodeCnt) {
    POLARIS_LOG(LOG_ERROR, "Invalid parameters. instanceSet/hashFunc is NULL, or vnodeCnt is zero");
    return false;
  }

  const std::vector<Instance*>& instances = instanceSet->GetInstances();
  if (instances.empty()) {
    POLARIS_LOG(LOG_ERROR, "No available instances");
    return false;
  }
  hashFunc_ = hashFunc;
  ring_.clear();
  ringLen_ = instances.size() * vnodeCnt;
  ring_.reserve(ringLen_);

#if __cplusplus >= 201103L
  std::unordered_map<uint64_t, uint64_t> hashVal2Key;  // 改成 unordered_map -71ms（124ms => 53ms)
  std::unordered_map<uint64_t, uint64_t>::iterator hashIt;
#else
  std::map<uint64_t, uint64_t> hashVal2Key;
  std::map<uint64_t, uint64_t>::iterator hashIt;
#endif
  uint32_t total = 0;
  for (size_t i = 0; i < instances.size(); ++i) {
    hashVal2Key[instances[i]->GetHash()] = ((uint64_t)i) << 32 | 0;
    total += instances[i]->GetWeight();
  }

  double percent = 0;
  char buff[128];
  double totalWeight = static_cast<double>(total);
  ContinuumPoint cp(0, 0);
  for (size_t i = 0; i < instances.size(); ++i) {
    Instance* inst                   = instances[i];
    InstanceLocalValue* localValue   = inst->GetLocalValue();
    std::vector<uint64_t>& vnodeHash = localValue->AcquireVnodeHash();
    percent                          = static_cast<double>(inst->GetWeight()) / totalWeight;
    int limit  = static_cast<int>(floor(static_cast<double>(ringLen_) * percent)) - 1;
    cp.hashVal = inst->GetHash();
    cp.index   = i;
    ring_.push_back(cp);  // 添加真实节点

    int hashCnt = vnodeHash.size();
    uint64_t hashVal;
    uint64_t keyHi = ((uint64_t)i) << 32;
    for (int k = 0, j = 1; k < limit; ++k) {
      int retry = 1;
      do {
        if (POLARIS_LIKELY(1 == retry && k < hashCnt)) {  // 不需要计算哈希值
          hashVal = vnodeHash[k];
        } else {
          memset(buff, 0, sizeof(buff));
          snprintf(buff, sizeof(buff), "%s:%d", inst->GetId().c_str(), j++);
          hashVal = hashFunc(static_cast<const void*>(buff), strlen(buff), 0);
        }
        hashIt = hashVal2Key.find(hashVal);
        if (POLARIS_LIKELY(hashIt == hashVal2Key.end())) {
          hashVal2Key[hashVal] = keyHi | j;
          cp.hashVal           = hashVal;
          ring_.push_back(cp);
          if (k >= hashCnt) {  // 哈希值不足, 添加进去
            vnodeHash.push_back(hashVal);
          } else if (retry > 1) {  // 哈希有冲突, 更新一下
            vnodeHash[k] = hashVal;
          }
          break;
        }
        // conflict
        POLARIS_LOG(LOG_WARN, "hash conflict between %s:%" PRIu64 " and %s",  // instanceId:j
                    instances[hashIt->second >> 32]->GetId().c_str(), hashIt->second & 0xFFFFFFFF,
                    buff);
      } while (++retry <= kMaxRehashIteration);
      if (retry > kMaxRehashIteration) {
        POLARIS_LOG(LOG_ERROR, "fail to generate hash @ %s:%u(id=%s limit=%d). reach %d tries",
                    inst->GetHost().c_str(), inst->GetPort(), inst->GetId().c_str(), k,
                    kMaxRehashIteration);
      }
    }

    if ((vnodeCnt * 2) >= static_cast<uint32_t>(limit * 3)) {  // 记录的 hash 值大于需要的 1.5 倍
      vnodeHash.resize(limit);
    }
    localValue->ReleaseVnodeHash();
  }

  ringLen_ = ring_.size();
  if (ring_.size() > 1) {
    std::sort(ring_.begin(), ring_.end());
  }
  return true;
}

int ContinuumSelector::Select(const Criteria& criteria) {
  if (0 == ringLen_) {
    return -1;
  } else if (1 == ringLen_) {
    return ring_[0].index;
  }
  uint64_t hash_value;
  if (!criteria.hash_string_.empty()) {
    const std::string& hash_key = criteria.hash_string_;
    hash_value = hashFunc_(static_cast<const void*>(hash_key.c_str()), hash_key.size(), 0);
  } else {
    if (POLARIS_UNLIKELY(0 == criteria.hash_key_)) {
      char buff[128];
      memset(buff, 0, sizeof(buff));
      snprintf(buff, sizeof(buff), "ringhash-%ld-%d", time(NULL), rand());
      hash_value = hashFunc_(static_cast<const void*>(buff), strlen(buff), 0);
    } else {
      hash_value = hashFunc_(static_cast<const void*>(&criteria.hash_key_), sizeof(uint64_t), 0);
    }
  }

  std::vector<ContinuumPoint>::iterator position =
      std::lower_bound(ring_.begin(), ring_.end(), hash_value);
  for (int i = 0; i < criteria.replicate_index_; ++i) {
    if (POLARIS_UNLIKELY(ring_.end() == position)) {
      position = ring_.begin();
    }
    position++;
  }
  if (POLARIS_LIKELY(ring_.end() == position)) {
    position = ring_.begin();
  }
  return position->index;
}

uint32_t ContinuumSelector::CalcTotalWeight(const std::vector<Instance*>& vctInstances) {
  uint32_t total = 0;
  for (std::vector<Instance*>::const_iterator it = vctInstances.begin(); it != vctInstances.end();
       ++it) {
    total += (*it)->GetWeight();
  }
  return total;
}

uint32_t ContinuumSelector::CalcMaxWeight(const std::vector<Instance*>& vctInstances) {
  uint32_t max_weight = 0;
  for (std::vector<Instance*>::const_iterator it = vctInstances.begin(); it != vctInstances.end();
       ++it) {
    if ((*it)->GetWeight() > max_weight) {
      max_weight = (*it)->GetWeight();
    }
  }
  return max_weight;
}

bool ContinuumSelector::ReHash(int iteration, uint64_t& hash_value,
                               std::map<uint64_t, std::string>& hash_value_key) {
  if (iteration > kMaxRehashIteration) {
    return false;
  }
  std::string hash_str = StringUtils::TypeToStr(hash_value);
  hash_value           = hashFunc_(static_cast<const void*>(hash_str.c_str()), hash_str.size(), 0);
  std::map<uint64_t, std::string>::iterator hash_it = hash_value_key.find(hash_value);
  if (POLARIS_LIKELY(hash_it == hash_value_key.end())) {
    hash_value_key[hash_value] = hash_str;
    return true;
  }
  return ReHash(iteration + 1, hash_value, hash_value_key);
}

}  // namespace polaris