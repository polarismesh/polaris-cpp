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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_PLUGIN_MANAGER_H_
#define POLARIS_CPP_POLARIS_PLUGIN_PLUGIN_MANAGER_H_

#include <map>
#include <string>
#include <vector>

#include "polaris/defs.h"
#include "polaris/plugin.h"
#include "sync/mutex.h"

namespace polaris {

class ServiceData;

const char kPluginDefaultServerConnector[] = "grpc";
const char kPluginDefaultLocalRegistry[]   = "inmemory";
const char kPluginDefaultStatReporter[]    = "default";
const char kPluginDefaultAlertReporter[]   = "default";

const char kPluginDefaultWeightAdjuster[] = "default";

const char kPluginRuleServiceRouter[]        = "ruleBasedRouter";
const char kPluginNearbyServiceRouter[]      = "nearbyBasedRouter";
const char kPluginSetDivisionServiceRouter[] = "setDivisionRouter";
const char kPluginMetadataServiceRouter[]    = "dstMetaRouter";
const char kPluginCanaryServiceRouter[]      = "canaryRouter";
const char kPluginRuleServiceRouterAlias[]   = "ruleRouter";
const char kPluginNearbyServiceRouterAlias[] = "nearbyRouter";

const char kPluginErrorCountCircuitBreaker[] = "errorCount";
const char kPluginErrorRateCircuitBreaker[]  = "errorRate";

const char kPluginHttpHealthChecker[] = "http";
const char kPluginTcpHealthChecker[]  = "tcp";
const char kPluginUdpHealthChecker[]  = "udp";
const char* PluginTypeToString(PluginType plugin_type);

/// @brief 管理插件，全局只初始化一个对象
class PluginManager {
public:
  PluginManager();

  ~PluginManager();

  ReturnCode RegisterPlugin(const std::string& name, PluginType plugin_type,
                            PluginFactory plugin_factory);

  ReturnCode GetPlugin(const std::string& name, PluginType plugin_type, Plugin*& plugin);

  ReturnCode RegisterInstancePreUpdateHandler(InstancePreUpdateHandler handler,
                                              bool bFront = false);
  ReturnCode DeregisterInstancePreUpdateHandler(InstancePreUpdateHandler handler);

  void OnPreUpdateServiceData(ServiceData* oldData, ServiceData* newData);

  static PluginManager& Instance();

  // 通过类型创建负载均衡插件
  //  ReturnCode GetLoadBalancePlugin(LoadBalanceType load_balace_type, Plugin*& plugin);

private:
  sync::Mutex lock_;
  std::map<std::string, PluginFactory> plugin_factory_map_;

  sync::Mutex instancePreUpdatelock_;
  std::vector<InstancePreUpdateHandler> instancePreUpdateHandlers_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_PLUGIN_MANAGER_H_
