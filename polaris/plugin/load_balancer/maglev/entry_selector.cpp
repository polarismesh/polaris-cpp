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

#include "plugin/load_balancer/maglev/entry_selector.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <memory>

#include "logger.h"
#include "polaris/model.h"

namespace polaris {

MaglevEntrySelector::MaglevEntrySelector() : hash_func_(nullptr), table_size_(65537) {}

MaglevEntrySelector::~MaglevEntrySelector() {}

bool MaglevEntrySelector::Setup(InstancesSet* instance_set, uint32_t table_size, Hash64Func hash_func) {
  if (nullptr == instance_set || nullptr == hash_func || 0 == table_size) {
    POLARIS_LOG(LOG_ERROR, "Invalid parameters. instance_set/hashFunc is nullptr, or tableSize is zero");
    return false;
  }

  const std::vector<Instance*>& instances = instance_set->GetInstances();
  size_t count = instances.size();
  if (0 == count) {
    POLARIS_LOG(LOG_ERROR, "No available instances");
    return false;
  }
  if (table_size < count) {
    if (count > 655373) {  // two big prime, copy from golang maglev implementation
      POLARIS_LOG(LOG_ERROR, "Too many instances(> 655373), please config maglev.tableSize");
      return false;
    } else if (count > 65537) {
      table_size = 655373;
    } else {
      table_size = 65537;
    }
  }
  entries_.clear();
  table_size_ = table_size;
  hash_func_ = hash_func;
  constexpr uint32_t INVALID = static_cast<uint32_t>(-1);
  std::vector<uint32_t> entry(table_size_, INVALID);

  std::vector<Slot> slots;
  double max_weight = GenerateOffsetAndSkips(instances, slots);  // gen permutation table
  uint32_t fill_count = 0;
  for (uint32_t iteration = 1; fill_count < table_size_; ++iteration) {
    for (uint32_t i = 0; i < count && fill_count < table_size_; ++i) {
      Slot& slot = slots[i];
      // skip if not its(current slot's) turn: accumulated weight NOT reach
      if (static_cast<double>(iteration) * slot.normalized_weight < slot.target_weight) {
        continue;
      }
      slot.target_weight += max_weight;  // next target weigth
      uint32_t idx = 0;
      do {
        idx = Permutation(slot);
        ++slot.next;
      } while (entry[idx] != INVALID);

      entry[idx] = slot.index;
      ++slot.next;
      ++slot.count;
      ++fill_count;
    }
  }
  entry.swap(entries_);

  uint32_t min_entries = table_size_;
  uint32_t max_entries = 0;
  for (size_t i = 0; i < count; ++i) {
    min_entries = (min_entries > slots[i].count) ? slots[i].count : min_entries;
    max_entries = (max_entries < slots[i].count) ? slots[i].count : max_entries;
  }
  POLARIS_LOG(LOG_DEBUG, "maglev| build entries of %zu slots. min_entries %u max_entries %u", slots.size(), min_entries,
              max_entries);
  return true;
}

int MaglevEntrySelector::Select(const Criteria& criteria) {
  if (0 == table_size_) {
    return -1;
  } else if (1 == table_size_) {
    return 0;
  }
  uint64_t hash_value;
  if (criteria.hash_string_.empty()) {
    if (POLARIS_UNLIKELY(0 == criteria.hash_key_)) {
      char buff[128];
      memset(buff, 0, sizeof(buff));
      snprintf(buff, sizeof(buff), "maglev-%ld-%d", time(nullptr), rand());
      hash_value = hash_func_(static_cast<const void*>(buff), strlen(buff), 0);
    } else {
      hash_value = hash_func_(static_cast<const void*>(&criteria.hash_key_), sizeof(uint64_t), 0);
    }
  } else {
    const std::string& hash_key = criteria.hash_string_;
    hash_value = hash_func_(static_cast<const void*>(hash_key.c_str()), hash_key.size(), 0);
  }
  return entries_[hash_value % table_size_];
}

double MaglevEntrySelector::GenerateOffsetAndSkips(const std::vector<Instance*>& instances, std::vector<Slot>& slots) {
  std::size_t instance_count = instances.size();
  slots.reserve(instance_count);
  slots.clear();
  double total_weight = static_cast<double>(InstancesSetImpl::CalcTotalWeight(instances));
  char buff[128] = {0};
  double max_weight = 0;
  for (std::size_t i = 0; i < instance_count; ++i) {
    Instance* inst = instances[i];
    Slot slot;
    slot.normalized_weight = static_cast<double>(inst->GetWeight()) / total_weight;
    if (max_weight < slot.normalized_weight) {
      max_weight = slot.normalized_weight;
    }
    memset(buff, 0, sizeof(buff));
    snprintf(buff, sizeof(buff), "%s:%d", inst->GetHost().c_str(), inst->GetPort());
    uint32_t len = strlen(buff);
    uint64_t seed0 = hash_func_(buff, len, 1);
    uint64_t seed1 = hash_func_(buff, len, 2);
    slot.index = i;
    slot.offset = seed0 % table_size_;
    slot.skip = seed1 % (table_size_ - 1) + 1;
    slots.push_back(slot);
  }
  return max_weight;
}

}  // namespace polaris
