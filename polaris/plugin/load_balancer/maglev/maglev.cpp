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
#include "plugin/load_balancer/maglev/maglev_entry_selector.h"
#include "polaris/config.h"
#include "polaris/model.h"
#include "utils/utils.h"

namespace polaris {

class Context;

MaglevLoadBalancer::MaglevLoadBalancer() : context_(NULL), hash_func_(NULL), table_size_(0) {}

MaglevLoadBalancer::~MaglevLoadBalancer() { context_ = NULL; }

ReturnCode MaglevLoadBalancer::Init(Config* config, Context* context) {
  static const uint32_t kDefaultTableSize  = 65537;  // smallM, bigM=655373, must be prime
  static const char kLookupTableSize[]     = "tableSize";
  static const char kHashFunction[]        = "hashFunc";
  static const char kHashFunctionDefault[] = "murmur3";
  table_size_ = config->GetIntOrDefault(kLookupTableSize, kDefaultTableSize);
  if (!Utils::IsPrime(table_size_)) {
    POLARIS_LOG(LOG_ERROR,
                "Invalid parameters. maglev.tableSize MUST be PRIME"
                " and greator than size of instance set");
    return kReturnInvalidConfig;
  }
  std::string hashFunc = config->GetStringOrDefault(kHashFunction, kHashFunctionDefault);
  ReturnCode code      = HashManager::Instance().GetHashFunction(hashFunc, hash_func_);
  if (code != kReturnOk) {
    return code;
  }
  context_ = context;
  return kReturnOk;
}

ReturnCode MaglevLoadBalancer::ChooseInstance(ServiceInstances* service_instance,
                                              const Criteria& criteria, Instance*& next) {
  next                        = NULL;
  InstancesSet* instances_set = service_instance->GetAvailableInstances();
  POLARIS_ASSERT(instances_set != NULL);
  Selector* tmpSelector         = instances_set->GetSelector();
  MaglevEntrySelector* selector = NULL;
  if (POLARIS_LIKELY(tmpSelector != NULL)) {
    selector = dynamic_cast<MaglevEntrySelector*>(tmpSelector);
  }

  if (selector == NULL) {  // construct selector
    instances_set->AcquireSelectorCreationLock();
    tmpSelector = instances_set->GetSelector();
    if (tmpSelector != NULL) {
      selector = dynamic_cast<MaglevEntrySelector*>(tmpSelector);
    } else {
      selector = new MaglevEntrySelector();
      if (!selector->Setup(instances_set, table_size_, hash_func_)) {
        instances_set->ReleaseSelectorCreationLock();
        delete selector;
        return kReturnInvalidConfig;
      }
      instances_set->SetSelector(selector);
    }
    instances_set->ReleaseSelectorCreationLock();
  }

  int index = selector->Select(criteria);
  if (-1 != index) {
    const std::vector<Instance*>& vctInstances = instances_set->GetInstances();
    next                                       = vctInstances[index];
    return kReturnOk;
  }
  return kReturnInstanceNotFound;
}

}  // namespace polaris