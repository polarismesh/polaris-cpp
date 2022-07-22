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

#include "maglev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>
#include <vector>

#include "logger.h"
#include "model/model_impl.h"
#include "plugin/load_balancer/maglev/entry_selector.h"
#include "polaris/config.h"
#include "polaris/model.h"
#include "utils/utils.h"

namespace polaris {

class Context;

MaglevLoadBalancer::MaglevLoadBalancer() : context_(nullptr), hash_func_(nullptr), table_size_(0) {}

MaglevLoadBalancer::~MaglevLoadBalancer() { context_ = nullptr; }

ReturnCode MaglevLoadBalancer::Init(Config* config, Context* context) {
  static const uint32_t kDefaultTableSize = 65537;  // smallM, bigM=655373, must be prime
  static const char kLookupTableSize[] = "tableSize";
  static const char kHashFunction[] = "hashFunc";
  static const char kHashFunctionDefault[] = "murmur3";
  table_size_ = config->GetIntOrDefault(kLookupTableSize, kDefaultTableSize);
  if (!Utils::IsPrime(table_size_)) {
    POLARIS_LOG(LOG_ERROR, "Invalid parameters. tableSize MUST be PRIME and greater than size of instance set");
    return kReturnInvalidConfig;
  }
  std::string hashFunc = config->GetStringOrDefault(kHashFunction, kHashFunctionDefault);
  ReturnCode code = HashManager::Instance().GetHashFunction(hashFunc, hash_func_);
  if (code != kReturnOk) {
    return code;
  }
  context_ = context;
  return kReturnOk;
}

ReturnCode MaglevLoadBalancer::ChooseInstance(ServiceInstances* service_instances, const Criteria& criteria,
                                              Instance*& next) {
  next = nullptr;
  InstancesSet* instances_set = service_instances->GetAvailableInstances();
  POLARIS_ASSERT(instances_set != nullptr);
  Selector* instances_selector = instances_set->GetSelector();
  MaglevEntrySelector* selector = nullptr;
  if (POLARIS_LIKELY(instances_selector != nullptr)) {
    selector = dynamic_cast<MaglevEntrySelector*>(instances_selector);
  }

  if (selector == nullptr) {  // construct selector
    std::lock_guard<std::mutex> lock_guard(instances_set->GetImpl()->CreationLock());
    instances_selector = instances_set->GetSelector();
    if (instances_selector != nullptr) {
      selector = dynamic_cast<MaglevEntrySelector*>(instances_selector);
    } else {
      selector = new MaglevEntrySelector();
      if (!selector->Setup(instances_set, table_size_, hash_func_)) {
        delete selector;
        return kReturnInvalidConfig;
      }
      instances_set->SetSelector(selector);
    }
  }

  int index = selector->Select(criteria);
  if (-1 != index) {
    const std::vector<Instance*>& instances = instances_set->GetInstances();
    next = instances[index];
    return kReturnOk;
  }
  return kReturnInstanceNotFound;
}

}  // namespace polaris