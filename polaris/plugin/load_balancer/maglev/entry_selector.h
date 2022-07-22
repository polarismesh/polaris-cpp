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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_MAGLEV_ENTRY_SELECTOR_H_
#define POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_MAGLEV_ENTRY_SELECTOR_H_

#include <stdint.h>
#include <string>
#include <vector>

#include "model/model_impl.h"
#include "plugin/load_balancer/hash/hash_manager.h"
#include "polaris/instance.h"

namespace polaris {

// 中间建表的的数据
struct Slot {
  uint64_t offset;
  uint64_t skip;
  uint32_t index;
  uint32_t count;
  double normalized_weight;
  double target_weight;
  uint64_t next;

  Slot() : offset(0), skip(0), index(0), count(0), normalized_weight(0.0), target_weight(0.0), next(0) {}
};

class MaglevEntrySelector : public Selector {
 public:
  MaglevEntrySelector();

  virtual ~MaglevEntrySelector();

  /// @desc setup maglev lookup table
  ///
  /// @param instance_set: nodes to build lookup table
  /// @param table_size: lookup table size, MUST be PRIME and GREATER than size of instance_set
  /// @param hash_func: hash function to use
  ///
  /// @return bool: true - succ, false - false
  bool Setup(InstancesSet* instance_set, uint32_t table_size, Hash64Func hash_func);

  virtual int Select(const Criteria& criteria);

 private:
  double GenerateOffsetAndSkips(const std::vector<Instance*>& instances, std::vector<Slot>& slots);

  uint32_t Permutation(const Slot& slot) { return (slot.offset + slot.skip * slot.next) % table_size_; }

 private:
  Hash64Func hash_func_;
  std::vector<uint32_t> entries_;  // lookup table
  uint32_t table_size_;            // lookup table size
};

}  // namespace polaris
#endif  // POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_MAGLEV_ENTRY_SELECTOR_H_
