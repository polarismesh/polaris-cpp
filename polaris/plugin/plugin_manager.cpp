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

#include "plugin/plugin_manager.h"

#include <stddef.h>

#include <map>
#include <string>
#include <utility>

#include "logger.h"
#include "model/model_impl.h"
#include "plugin/alert_reporter/alert_reporter.h"
#include "plugin/circuit_breaker/error_count.h"
#include "plugin/circuit_breaker/error_rate.h"
#include "plugin/health_checker/http_detector.h"
#include "plugin/health_checker/tcp_detector.h"
#include "plugin/health_checker/udp_detector.h"
#include "plugin/load_balancer/l5_csthash.h"
#include "plugin/load_balancer/locality_aware/locality_aware.h"
#include "plugin/load_balancer/maglev/maglev.h"
#include "plugin/load_balancer/ringhash/ringhash.h"
#include "plugin/load_balancer/simple_hash.h"
#include "plugin/load_balancer/weighted_random.h"
#include "plugin/local_registry/local_registry.h"
#include "plugin/server_connector/server_connector.h"
#include "plugin/service_router/canary_router.h"
#include "plugin/service_router/metadata_router.h"
#include "plugin/service_router/nearby_router.h"
#include "plugin/service_router/rule_router.h"
#include "plugin/service_router/set_division_router.h"
#include "plugin/stat_reporter/stat_reporter.h"
#include "plugin/weight_adjuster/weight_adjuster.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "utils/indestructible.h"
#include "utils/static_assert.h"

namespace polaris {

#define TO_STR(value) #value

static const char* g_PluginTypeString[] = {
    TO_STR(kPluginServerConnector), TO_STR(kPluginLocalRegistry), TO_STR(kPluginServiceRouter),
    TO_STR(kPluginLoadBalancer),    TO_STR(kPluginHealthChecker), TO_STR(kPluginCircuitBreaker),
    TO_STR(kPluginWeightAdjuster),  TO_STR(kPluginStatReporter),  TO_STR(kPluginAlertReporter),
    TO_STR(kPluginServerMetric)};

STATIC_ASSERT(sizeof(g_PluginTypeString) / sizeof(const char*) == kPluginTypeMaxCount,
              "plugin type define error");

ReturnCode RegisterPlugin(std::string name, PluginType plugin_type, PluginFactory plugin_factory) {
  return PluginManager::Instance().RegisterPlugin(name, plugin_type, plugin_factory);
}

const char* PluginTypeToString(PluginType plugin_type) {
  POLARIS_ASSERT(plugin_type < kPluginTypeMaxCount);
  return g_PluginTypeString[plugin_type];
}

PluginManager& PluginManager::Instance() {
  static Indestructible<PluginManager> plugin_manager;
  return *plugin_manager.Get();
}

Plugin* GrpcServerConnectorFactory() { return new GrpcServerConnector(); }
Plugin* InMemoryRegistryFactory() { return new InMemoryRegistry(); }
Plugin* MonitorStatReporterFactory() { return new MonitorStatReporter(); }
Plugin* LogAlertReporterFactory() { return new LogAlertReporter(); }

Plugin* RandomLoadBalancerFactory() { return new RandomLoadBalancer(); }
Plugin* RingHashLoadBalancerFactory() { return new KetamaLoadBalancer(); }
Plugin* MaglevLoadBalancerFactory() { return new MaglevLoadBalancer(); }
Plugin* L5CstHashLoadBalancerFactory() { return new L5CstHashLoadBalancer(); }
Plugin* SimpleHashLoadBalancerFactory() { return new SimpleHashLoadBalancer(); }
Plugin* CMurmurHashLoadBalancerFactory() { return new L5CstHashLoadBalancer(true); }
Plugin* LocalityAwareLoadBalancerFactory() { return new LocalityAwareLoadBalancer(); }
Plugin* DefaultWeightAdjusterFactory() { return new DefaultWeightAdjuster(); }

Plugin* RuleServiceRouterFactory() { return new RuleServiceRouter(); }
Plugin* NearbyServiceRouterFactory() { return new NearbyServiceRouter(); }
Plugin* SetDivisionServiceRouterFactory() { return new SetDivisionServiceRouter(); }
Plugin* CanaryServiceRouterFactory() { return new CanaryServiceRouter(); }
Plugin* MetadataServiceRouterFactory() { return new MetadataServiceRouter(); }

Plugin* ErrorCountCircuitBreakerFactory() { return new ErrorCountCircuitBreaker(); }
Plugin* ErrorRateCircuitBreakerFactory() { return new ErrorRateCircuitBreaker(); }

Plugin* HttpHealthCheckerFactory() { return new HttpHealthChecker(); }
Plugin* TcpHealthCheckerFactory() { return new TcpHealthChecker(); }
Plugin* UdpHealthCheckerFactory() { return new UdpHealthChecker(); }

PluginManager::PluginManager() {
  RegisterPlugin(kPluginDefaultServerConnector, kPluginServerConnector, GrpcServerConnectorFactory);
  RegisterPlugin(kPluginDefaultLocalRegistry, kPluginLocalRegistry, InMemoryRegistryFactory);
  RegisterPlugin(kPluginDefaultStatReporter, kPluginStatReporter, MonitorStatReporterFactory);
  RegisterPlugin(kPluginDefaultAlertReporter, kPluginAlertReporter, LogAlertReporterFactory);

  RegisterPlugin(kLoadBalanceTypeWeightedRandom, kPluginLoadBalancer, RandomLoadBalancerFactory);
  RegisterPlugin(kLoadBalanceTypeRingHash, kPluginLoadBalancer, RingHashLoadBalancerFactory);
  RegisterPlugin(kLoadBalanceTypeMaglevHash, kPluginLoadBalancer, MaglevLoadBalancerFactory);
  RegisterPlugin(kLoadBalanceTypeL5CstHash, kPluginLoadBalancer, L5CstHashLoadBalancerFactory);
  RegisterPlugin(kLoadBalanceTypeSimpleHash, kPluginLoadBalancer, SimpleHashLoadBalancerFactory);
  RegisterPlugin(kLoadBalanceTypeLocalityAware, kPluginLoadBalancer,
                 LocalityAwareLoadBalancerFactory);
  RegisterPlugin(kLoadBalanceTypeCMurmurHash, kPluginLoadBalancer, CMurmurHashLoadBalancerFactory);

  RegisterPlugin(kPluginDefaultWeightAdjuster, kPluginWeightAdjuster, DefaultWeightAdjusterFactory);

  RegisterPlugin(kPluginRuleServiceRouter, kPluginServiceRouter, RuleServiceRouterFactory);
  RegisterPlugin(kPluginNearbyServiceRouter, kPluginServiceRouter, NearbyServiceRouterFactory);
  RegisterPlugin(kPluginSetDivisionServiceRouter, kPluginServiceRouter,
                 SetDivisionServiceRouterFactory);
  RegisterPlugin(kPluginCanaryServiceRouter, kPluginServiceRouter, CanaryServiceRouterFactory);
  RegisterPlugin(kPluginMetadataServiceRouter, kPluginServiceRouter, MetadataServiceRouterFactory);

  RegisterPlugin(kPluginErrorCountCircuitBreaker, kPluginCircuitBreaker,
                 ErrorCountCircuitBreakerFactory);
  RegisterPlugin(kPluginErrorRateCircuitBreaker, kPluginCircuitBreaker,
                 ErrorRateCircuitBreakerFactory);

  RegisterPlugin(kPluginHttpHealthChecker, kPluginHealthChecker, HttpHealthCheckerFactory);
  RegisterPlugin(kPluginTcpHealthChecker, kPluginHealthChecker, TcpHealthCheckerFactory);
  RegisterPlugin(kPluginUdpHealthChecker, kPluginHealthChecker, UdpHealthCheckerFactory);
}

PluginManager::~PluginManager() {}

ReturnCode PluginManager::RegisterPlugin(const std::string& name, PluginType plugin_type,
                                         PluginFactory plugin_factory) {
  std::string name_with_type = name + PluginTypeToString(plugin_type);
  sync::MutexGuard mutex_guard(lock_);
  std::map<std::string, PluginFactory>::iterator it = plugin_factory_map_.find(name_with_type);
  if (it != plugin_factory_map_.end() && it->second != plugin_factory) {
    POLARIS_LOG(LOG_ERROR, "register plugin failed: plugin type %s with name %s already exist",
                PluginTypeToString(plugin_type), name.c_str());
    return kReturnPluginError;
  }
  plugin_factory_map_[name_with_type] = plugin_factory;
  return kReturnOk;
}

ReturnCode PluginManager::GetPlugin(const std::string& name, PluginType plugin_type,
                                    Plugin*& plugin) {
  std::string name_with_type = name + PluginTypeToString(plugin_type);
  lock_.Lock();
  std::map<std::string, PluginFactory>::iterator it = plugin_factory_map_.find(name_with_type);
  if (it == plugin_factory_map_.end()) {
    POLARIS_LOG(LOG_ERROR, "get plugin error: plugin type %s with name %s not exist",
                PluginTypeToString(plugin_type), name.c_str());
    lock_.Unlock();
    return kReturnPluginError;
  }
  PluginFactory plugin_factory = it->second;
  lock_.Unlock();
  plugin = plugin_factory();
  return kReturnOk;
}

ReturnCode PluginManager::RegisterInstancePreUpdateHandler(InstancePreUpdateHandler handler,
                                                           bool bFront /* = false*/) {
  sync::MutexGuard mutex_guard(instancePreUpdatelock_);
  std::vector<InstancePreUpdateHandler>::iterator it = instancePreUpdateHandlers_.begin();
  for (; it != instancePreUpdateHandlers_.end(); ++it) {
    if (*it == handler) {
      return kReturnExistedResource;
    }
  }
  if (bFront) {
    instancePreUpdateHandlers_.insert(instancePreUpdateHandlers_.begin(), handler);
  } else {
    instancePreUpdateHandlers_.push_back(handler);
  }
  return kReturnOk;
}

ReturnCode PluginManager::DeregisterInstancePreUpdateHandler(InstancePreUpdateHandler handler) {
  sync::MutexGuard mutex_guard(instancePreUpdatelock_);
  std::vector<InstancePreUpdateHandler>::iterator it = instancePreUpdateHandlers_.begin();
  for (; it != instancePreUpdateHandlers_.end(); ++it) {
    if (*it == handler) {
      instancePreUpdateHandlers_.erase(it);
      return kReturnOk;
    }
  }
  return kReturnPluginError;
}

void PluginManager::OnPreUpdateServiceData(ServiceData* oldData, ServiceData* newData) {
  if (NULL == newData || NULL == oldData || 0 == instancePreUpdateHandlers_.size()) {
    return;
  }
  instancePreUpdatelock_.Lock();
  std::vector<InstancePreUpdateHandler> handlers(instancePreUpdateHandlers_.begin(),
                                                 instancePreUpdateHandlers_.end());
  instancePreUpdatelock_.Unlock();
  std::vector<InstancePreUpdateHandler>::iterator it = handlers.begin();
  for (; it != handlers.end(); ++it) {
    (*it)(oldData->GetServiceDataImpl()->data_.instances_,
          newData->GetServiceDataImpl()->data_.instances_);
  }
  return;
}

}  // namespace polaris
