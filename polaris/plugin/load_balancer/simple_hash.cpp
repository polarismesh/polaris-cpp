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

#include "plugin/load_balancer/simple_hash.h"

#include <stddef.h>

#include <set>
#include <vector>

#include "model/model_impl.h"
#include "polaris/model.h"

namespace polaris {

ReturnCode SimpleHashLoadBalancer::ChooseInstance(ServiceInstances* service_instances, const Criteria& criteria,
                                                  Instance*& next) {
  next = nullptr;
  InstancesSet* instances_set = service_instances->GetAvailableInstances();
  std::vector<Instance*> instances = instances_set->GetInstances();
  std::set<Instance*> half_open_instances;
  service_instances->GetHalfOpenInstances(half_open_instances);

  if (!criteria.ignore_half_open_) {
    service_instances->GetService()->TryChooseHalfOpenInstance(half_open_instances, next);
    if (next != nullptr) {
      return kReturnOk;
    }
  }

  if (instances.empty()) {
    return kReturnInstanceNotFound;
  }

  next = instances[criteria.hash_key_ % instances.size()];
  if (half_open_instances.count(next) > 0 && instances.size() != half_open_instances.size()) {
    // 选到了半开节点，且有正常的节点，则查找到正常的节点
    for (std::size_t i = 1; i < instances.size(); ++i) {
      next = instances[(criteria.hash_key_ + i) % instances.size()];
      if (half_open_instances.count(next) == 0) {
        return kReturnOk;
      }
    }
  }
  return kReturnOk;
}

}  // namespace polaris
