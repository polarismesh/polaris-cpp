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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_SERVICE_ROUTER_H_
#define POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_SERVICE_ROUTER_H_

#include <stddef.h>
#include <stdint.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "polaris/defs.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "v1/request.pb.h"

namespace polaris {

namespace ServiceRouterConfig {
static const char kChainEnableKey[]   = "enable";
static const bool kChainEnableDefault = true;

static const char kChainPluginListKey[]     = "chain";
static const char kChainPluginListDefault[] = "ruleBasedRouter, nearbyBasedRouter";

static const char kRecoverAllEnableKey[]   = "enableRecoverAll";
static const bool kRecoverAllEnableDefault = true;

static const char kPercentOfMinInstancesKey[]    = "percentOfMinInstances";
static const float kPercentOfMinInstancesDefault = 0.0;

static const uint32_t kRuleDefaultPriority = 9;
static const uint32_t kRuleDefaultWeight   = 0;
}  // namespace ServiceRouterConfig

struct ServiceDataOrNotify {
  ServiceDataOrNotify() : service_data_(NULL), service_notify_(NULL) {}
  ~ServiceDataOrNotify() {
    if (service_data_ != NULL) {
      service_data_->DecrementRef();
      service_data_ = NULL;
    }
  }

  ServiceData* service_data_;
  ServiceDataNotify* service_notify_;
};

static const int kDataOrNotifySize = 3;

class RouteInfoNotifyImpl {
public:
  RouteInfoNotifyImpl() : all_data_ready_(false) {}

  ~RouteInfoNotifyImpl() {}

private:
  friend class ServiceRouterChain;
  friend class RouteInfoNotify;
  bool all_data_ready_;
  ServiceDataOrNotify data_or_notify_[kDataOrNotifySize];
};

// 服务路由执行链实现
class ServiceRouterChainImpl {
private:
  friend class ServiceRouterChain;
  Context* context_;
  ServiceKey service_key_;
  bool enable_;
  bool is_rule_router_enable_;
  std::vector<ServiceRouter*> service_router_list_;
  std::vector<std::string> plugin_name_list_;
};

void CalculateUnhealthySet(RouteInfo& route_info, ServiceInstances* service_instances,
                           std::set<Instance*>& unhealthy_set);

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_SERVICE_ROUTER_H_
