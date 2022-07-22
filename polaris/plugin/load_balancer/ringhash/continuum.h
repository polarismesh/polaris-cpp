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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_RINGHASH_CONTINUUM_H_
#define POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_RINGHASH_CONTINUUM_H_

#include <stdint.h>
#include <string>
#include <vector>

#include "model/model_impl.h"
#include "plugin/load_balancer/hash/hash_manager.h"
#include "polaris/instance.h"

namespace polaris {

// 哈希环的节点
struct ContinuumPoint {
  uint64_t hashVal;
  int index;

  ContinuumPoint(uint64_t val, int idx) : hashVal(val), index(idx) {}

  bool operator<(const ContinuumPoint& rhs) const { return this->hashVal < rhs.hashVal; }

  bool operator<(const uint64_t val) const { return this->hashVal < val; }
};

struct HashKeyIndex {
  std::size_t instance_index_;
  int vnode_index_;
};

// 一致性哈希环
class ContinuumSelector : public Selector {
 public:
  explicit ContinuumSelector(Hash64Func hash_func);

  virtual ~ContinuumSelector();

  virtual int Select(const Criteria& criteria);

  // 构建哈希环
  void Setup(const std::vector<Instance*>& instances, const std::set<Instance*>& half_open_instances,
             uint32_t vnode_cnt, int base_weight, bool dynamic_weight);

  void FastSetup(const std::vector<Instance*>& instances, const std::set<Instance*>& half_open_instances,
                 uint32_t vnode_cnt, int base_weight, bool dynamic_weight);

  bool ReHash(int iteration, uint64_t& hash_value, std::map<uint64_t, std::string>& hash_value_key);

  bool EmptyRing() const { return ring_.empty(); }

  uint64_t CalculateHashValue(const Criteria& criteria) const;

  ReturnCode SelectReplicate(const std::vector<Instance*>& instances, const Criteria& criteria, Instance*& next);

 private:
  Hash64Func hash_func_;              // 哈希函数
  std::vector<ContinuumPoint> ring_;  // 哈希环
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_RINGHASH_CONTINUUM_H_
