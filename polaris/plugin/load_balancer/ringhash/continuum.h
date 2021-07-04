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

namespace polaris {

class Instance;

// 哈希环的节点
struct ContinuumPoint {
  uint64_t hashVal;
  int index;

  ContinuumPoint(uint64_t val, int idx) : hashVal(val), index(idx) {}

  bool operator<(const ContinuumPoint& rhs) const { return this->hashVal < rhs.hashVal; }

  bool operator<(const uint64_t val) const { return this->hashVal < val; }
};

// 一致性哈希环
class ContinuumSelector : public Selector {
public:
  ContinuumSelector();

  virtual ~ContinuumSelector();

  virtual int Select(const Criteria& criteria);

  // 构建哈希环
  bool Setup(InstancesSet* instance_set, uint32_t vnode_cnt, Hash64Func hash_func);

  bool FastSetup(InstancesSet* instanceSet, uint32_t vnodeCnt, Hash64Func hashFunc);

private:
  uint32_t CalcTotalWeight(const std::vector<Instance*>& vctInstances);

  uint32_t CalcMaxWeight(const std::vector<Instance*>& vctInstances);

  bool ReHash(int iteration, uint64_t& hash_value, std::map<uint64_t, std::string>& hash_value_key);

private:
  Hash64Func hashFunc_;               // 哈希函数
  std::vector<ContinuumPoint> ring_;  // 哈希环
  uint32_t ringLen_;                  // 哈希环长度, 用于极端情况加速计算
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_RINGHASH_CONTINUUM_H_
