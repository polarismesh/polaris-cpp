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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_MAGLEV_MAGLEV_H_
#define POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_MAGLEV_MAGLEV_H_

#include <stdint.h>

#include "plugin/load_balancer/hash/hash_manager.h"
#include "polaris/defs.h"
#include "polaris/plugin.h"

namespace polaris {

class Config;
class Context;
class Instance;
class ServiceInstances;

class MaglevLoadBalancer : public LoadBalancer {
public:
  MaglevLoadBalancer();

  virtual ~MaglevLoadBalancer();

  virtual ReturnCode Init(Config* config, Context* content);

  virtual LoadBalanceType GetLoadBalanceType() { return kLoadBalanceTypeMaglevHash; }

  virtual ReturnCode ChooseInstance(ServiceInstances* instances, const Criteria& criteria,
                                    Instance*& next);

private:
  Context* context_;
  Hash64Func hash_func_;
  uint32_t table_size_;
};  // class MaglevLoadBalancer

}  // namespace polaris
#endif  // POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_MAGLEV_MAGLEV_H_