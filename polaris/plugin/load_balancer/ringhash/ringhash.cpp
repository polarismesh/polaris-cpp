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

#include "plugin/load_balancer/ringhash/ringhash.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <map>
#include <string>
#include <vector>

#include "model/model_impl.h"
#include "plugin/load_balancer/ringhash/continuum.h"
#include "plugin/plugin_manager.h"
#include "polaris/config.h"
#include "polaris/model.h"
#include "utils/utils.h"

namespace polaris {

KetamaLoadBalancer::KetamaLoadBalancer()
    : context_(NULL), vnodeCnt_(0), hashFunc_(NULL), compatible_go_(false) {}

KetamaLoadBalancer::~KetamaLoadBalancer() { context_ = NULL; }

ReturnCode KetamaLoadBalancer::Init(Config* config, Context* context) {
  context_ = context;

  static const char kVirtualNodeCount[]          = "vnodeCount";
  static const uint32_t kVirtualNodeCountDefault = 1024;
  static const char kHashFunction[]              = "hashFunc";
  static const char kHashFunctionDefault[]       = "murmur3";
  static const char kCompatibleGoKey[]           = "compatibleGo";
  static const bool kCompatibleGoDefault         = false;

  // 读配置, 加载虚拟节点数和哈希函数
  compatible_go_ = config->GetBoolOrDefault(kCompatibleGoKey, kCompatibleGoDefault);
  if (compatible_go_) {
    static const uint32_t kGoVirtualNodeCountDefault = 10;
    vnodeCnt_ = config->GetIntOrDefault(kVirtualNodeCount, kGoVirtualNodeCountDefault);
  } else {
    vnodeCnt_ = config->GetIntOrDefault(kVirtualNodeCount, kVirtualNodeCountDefault);
  }
  std::string hashFunc = config->GetStringOrDefault(kHashFunction, kHashFunctionDefault);
  ReturnCode code      = HashManager::Instance().GetHashFunction(hashFunc, hashFunc_);
  if (code != kReturnOk) {
    return code;
  }
  PluginManager::Instance().RegisterInstancePreUpdateHandler(KetamaLoadBalancer::OnInstanceUpdate);
  return kReturnOk;
}

ReturnCode KetamaLoadBalancer::ChooseInstance(ServiceInstances* service_instance,
                                              const Criteria& criteria, Instance*& next) {
  next                        = NULL;
  InstancesSet* instances_set = service_instance->GetAvailableInstances();
  Selector* tmpSelector       = instances_set->GetSelector();
  ContinuumSelector* selector = NULL;
  if (POLARIS_LIKELY(tmpSelector != NULL)) {
    selector = dynamic_cast<ContinuumSelector*>(tmpSelector);
  }

  if (selector == NULL) {  // 构造 selector
    instances_set->AcquireSelectorCreationLock();
    tmpSelector = instances_set->GetSelector();
    if (tmpSelector != NULL) {
      selector = dynamic_cast<ContinuumSelector*>(tmpSelector);
    } else {
      selector = new ContinuumSelector();
      if (compatible_go_) {
        if (!selector->Setup(instances_set, vnodeCnt_, hashFunc_)) {
          instances_set->ReleaseSelectorCreationLock();
          delete selector;
          return kReturnInvalidConfig;  // 只有参数错误才会失败
        }
      } else {
        if (!selector->FastSetup(instances_set, vnodeCnt_, hashFunc_)) {
          instances_set->ReleaseSelectorCreationLock();
          delete selector;
          return kReturnInvalidConfig;  // 只有参数错误才会失败
        }
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

void KetamaLoadBalancer::OnInstanceUpdate(const InstancesData* old_instances,
                                          InstancesData* new_instances) {
  std::map<std::string, Instance*>::iterator nIt  = new_instances->instances_map_.begin();
  std::map<std::string, Instance*>::iterator nEnd = new_instances->instances_map_.end();
  std::map<std::string, Instance*>::const_iterator oIt;
  const std::map<std::string, Instance*>& oldInstances  = old_instances->instances_map_;
  std::map<std::string, Instance*>::const_iterator oEnd = old_instances->instances_map_.end();
  for (; nIt != nEnd; ++nIt) {
    oIt = oldInstances.find(nIt->second->GetId());
    if (oIt != oEnd) {  // 迁移数据
      InstanceSetter nSetter(*(nIt->second));
      InstanceSetter oSetter(*(oIt->second));
      nSetter.CopyLocalValue(oSetter);
    }
  }
}

}  // namespace polaris
