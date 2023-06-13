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

#include "plugin/service_router/router_chain.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <map>
#include <memory>
#include <utility>

#include "logger.h"
#include "model/model_impl.h"
#include "plugin/plugin_manager.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "polaris/plugin.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

ServiceRouterChain::ServiceRouterChain(const ServiceKey& service_key)
    : context_(nullptr),
      service_key_(service_key),
      is_rule_router_enable_(false),
      is_set_router_enable_(false),
      is_canary_router_enable_(false) {}

ServiceRouterChain::~ServiceRouterChain() {
  for (std::size_t i = 0; i < service_router_list_.size(); i++) {
    delete service_router_list_[i];
  }
  service_router_list_.clear();
  context_ = nullptr;
}

ReturnCode ServiceRouterChain::Init(Config* config, Context* context) {
  context_ = context;
  static const char kChainEnableKey[] = "enable";
  static const bool kChainEnableDefault = true;
  bool enable = config->GetBoolOrDefault(kChainEnableKey, kChainEnableDefault);
  if (enable == false) {
    POLARIS_LOG(LOG_INFO, "service router for service[%s/%s] is disable", service_key_.namespace_.c_str(),
                service_key_.name_.c_str());
    return kReturnOk;
  }

  static const char kChainPluginListKey[] = "chain";
  static const char kChainPluginListDefault[] = "ruleBasedRouter, nearbyBasedRouter";
  plugin_name_list_ = config->GetListOrDefault(kChainPluginListKey, kChainPluginListDefault);
  if (plugin_name_list_.empty()) {
    POLARIS_LOG(LOG_ERROR, "router chain for service[%s/%s] config[enable] is true, but config[chain] is error",
                service_key_.namespace_.c_str(), service_key_.name_.c_str());
    return kReturnInvalidConfig;
  }

  Config* chain_config = config->GetSubConfig("plugin");
  Plugin* plugin = nullptr;
  ServiceRouter* service_router = nullptr;
  ReturnCode ret = kReturnOk;
  for (size_t i = 0; i < plugin_name_list_.size(); i++) {
    std::string& plugin_name = plugin_name_list_[i];
    // ServiceRouter别名转换,兼容老版本sdk
    if (plugin_name.compare(kPluginRuleServiceRouterAlias) == 0) {
      plugin_name = kPluginRuleServiceRouter;
    } else if (plugin_name.compare(kPluginNearbyServiceRouterAlias) == 0) {
      plugin_name = kPluginNearbyServiceRouter;
    }

    if ((ret = PluginManager::Instance().GetPlugin(plugin_name, kPluginServiceRouter, plugin)) != kReturnOk) {
      POLARIS_LOG(LOG_ERROR, "service router plugin with name[%s] for service[%s/%s] not found", plugin_name.c_str(),
                  service_key_.namespace_.c_str(), service_key_.name_.c_str());
      break;
    }
    if ((service_router = dynamic_cast<ServiceRouter*>(plugin)) == nullptr) {
      POLARIS_LOG(LOG_ERROR, "plugin with name[%s] and type[%s] for service[%s/%s] can not convert to service router",
                  plugin_name.c_str(), PluginTypeToString(kPluginServiceRouter), service_key_.namespace_.c_str(),
                  service_key_.name_.c_str());
      delete plugin;
      ret = kReturnInvalidConfig;
      break;
    }
    Config* plugin_config = chain_config->GetSubConfig(plugin_name);
    ret = service_router->Init(plugin_config, context);
    delete plugin_config;
    if (ret != kReturnOk) {
      POLARIS_LOG(LOG_ERROR, "init service router plugin[%s] for service[%s/%s] failed", plugin_name.c_str(),
                  service_key_.namespace_.c_str(), service_key_.name_.c_str());
      delete service_router;
      break;
    }
    service_router_list_.push_back(service_router);
    if (plugin_name.compare(kPluginRuleServiceRouter) == 0) {
      is_rule_router_enable_ = true;
    } else if (plugin_name.compare(kPluginSetDivisionServiceRouter)) {
      is_set_router_enable_ = true;
    } else if (plugin_name.compare(kPluginCanaryServiceRouter)) {
      is_canary_router_enable_ = true;
    }
  }
  delete chain_config;
  if (ret == kReturnOk) {
    POLARIS_LOG(LOG_INFO, "init service router plugin[%s] for service[%s/%s] success",
                StringUtils::JoinString(plugin_name_list_).c_str(), service_key_.namespace_.c_str(),
                service_key_.name_.c_str());
  }
  return ret;
}

ReturnCode ServiceRouterChain::DoRoute(RouteInfo& route_info, RouteResult* route_result) {
  POLARIS_CHECK_ARGUMENT(route_info.GetServiceInstances() != nullptr);
  ReturnCode ret;
  for (std::size_t index = 0; index < service_router_list_.size(); index++) {
    auto begin_time = std::chrono::steady_clock::now();
    ServiceRouter* router = service_router_list_[index];
    ret = router->DoRoute(route_info, route_result);
    auto end_time = std::chrono::steady_clock::now();
    auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - begin_time).count();
    POLARIS_LOG(LOG_DEBUG, "router(%s) ns(%s) svc(%s) do route cost(%ld ms)", router->Name().c_str(),
                route_info.GetServiceKey().namespace_.c_str(), route_info.GetServiceKey().name_.c_str(), delay);
    if (ret != kReturnOk) {
      POLARIS_LOG(LOG_ERROR, "run service router plugin[%s] for service[%s/%s] return error[%s]",
                  plugin_name_list_[index].c_str(), service_key_.namespace_.c_str(), service_key_.name_.c_str(),
                  ReturnCodeToMsg(ret).c_str());
      if (ret == kReturnRouteRuleNotMatch) {
        POLARIS_LOG(LOG_ERROR, "router not match with instances[%s], route[%s], source route[%s]",
                    route_info.GetServiceInstances()->GetServiceData()->ToJsonString().c_str(),
                    route_info.GetServiceRouteRule()->GetServiceData()->ToJsonString().c_str(),
                    route_info.GetSourceServiceRouteRule() == nullptr
                        ? "not use"
                        : route_info.GetSourceServiceRouteRule()->GetServiceData()->ToJsonString().c_str());
      }
      return ret;
    }
    if (route_result->isRedirect()) {  // 需要转发，提前退出
      return kReturnOk;
    }
  }
  return kReturnOk;
}

void ServiceRouterChain::CollectStat(ServiceKey& service_key, std::map<std::string, RouterStatData*>& stat_data) {
  service_key = service_key_;
  for (std::size_t i = 0; i < service_router_list_.size(); i++) {
    RouterStatData* data = service_router_list_[i]->CollectStat();
    if (data != nullptr) {
      data->record_.set_plugin_name(plugin_name_list_[i]);
      stat_data[plugin_name_list_[i]] = data;
    }
  }
}

ServiceData* ServiceRouterChain::PrepareServiceData(const ServiceKey& service_key, ServiceDataType data_type,
                                                    int notify_index, RouteInfoNotify*& notify) {
  LocalRegistry* local_registry = context_->GetLocalRegistry();
  ServiceData* service_data = nullptr;
  if (local_registry->GetServiceDataWithRef(service_key, data_type, service_data) == kReturnOk &&
      service_data->GetDataStatus() != kDataNotFound) {
    service_data->DecrementRef();
    return service_data;
  }
  if (notify == nullptr) {
    notify = new RouteInfoNotify();
  }
  local_registry->LoadServiceDataWithNotify(service_key, data_type, service_data,
                                            notify->data_or_notify_[notify_index].service_notify_);
  notify->data_or_notify_[notify_index].service_data_ = service_data;  // 可能有磁盘数据
  return nullptr;
}

RouteInfoNotify* ServiceRouterChain::PrepareRouteInfoWithNotify(RouteInfo& route_info) {
  RouteInfoNotify* notify = nullptr;

  if (route_info.GetServiceInstances() == nullptr) {  // 服务实例
    auto service_data = PrepareServiceData(route_info.GetServiceKey(), kServiceDataInstances, 0, notify);
    if (service_data != nullptr) {
      route_info.SetServiceInstances(new ServiceInstances(service_data));
    }
  }
  if (is_rule_router_enable_) {  // 服务路由
    if (route_info.GetServiceRouteRule() == nullptr) {
      auto service_data = PrepareServiceData(route_info.GetServiceKey(), kServiceDataRouteRule, 1, notify);
      if (service_data != nullptr) {
        route_info.SetServiceRouteRule(new ServiceRouteRule(service_data));
      }
    }
    if (route_info.GetSourceServiceRouteRule() != nullptr) {  // tRPC框架有传入主调的规则路由ServiceData
      return notify;
    }
    ServiceInfo* source_service_info = route_info.GetSourceServiceInfo();
    if (source_service_info != nullptr && !source_service_info->service_key_.name_.empty()) {
      // 源服务路由 没有上传route_rule 或 创建service_data失败
      auto service_data = PrepareServiceData(source_service_info->service_key_, kServiceDataRouteRule, 2, notify);
      if (service_data != nullptr) {
        route_info.SetSourceServiceRouteRule(new ServiceRouteRule(service_data));
      }
    }
  }
  return notify;
}

bool ServiceRouterChain::IsRuleRouterEnable() { return is_rule_router_enable_; }

ReturnCode ServiceRouterChain::PrepareRouteInfo(RouteInfo& route_info, uint64_t timeout) {
  RouteInfoNotify* route_info_notify = PrepareRouteInfoWithNotify(route_info);
  if (route_info_notify == nullptr) {
    return kReturnOk;
  }
  bool use_disk_data = false;
  if (!route_info_notify->IsDataReady(use_disk_data)) {
    timespec ts = Time::SteadyTimeAdd(timeout);
    ReturnCode ret = route_info_notify->WaitData(ts);
    if (ret == kReturnTimeout) {
      use_disk_data = true;
      if (!route_info_notify->IsDataReady(use_disk_data)) {
        delete route_info_notify;
        route_info_notify = nullptr;
        return kReturnTimeout;
      }
    }
  }
  ReturnCode ret_code = route_info_notify->SetDataToRouteInfo(route_info);
  delete route_info_notify;
  route_info_notify = nullptr;
  return ret_code;
}

}  // namespace polaris
