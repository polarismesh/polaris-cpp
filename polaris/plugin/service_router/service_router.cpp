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

#include "service_router.h"

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

RouteInfoNotify::RouteInfoNotify(RouteInfoNotifyImpl* impl) { impl_ = impl; }

RouteInfoNotify::~RouteInfoNotify() {
  if (impl_ != NULL) {
    delete impl_;
    impl_ = NULL;
  }
}

bool RouteInfoNotify::IsDataReady(bool use_disk_data) {
  if (impl_->all_data_ready_) {
    return true;
  }
  // 有等待的数据未准备好，检查看是否有磁盘加载的数据
  if (use_disk_data) {
    for (int i = 0; i < kDataOrNotifySize; ++i) {
      ServiceDataOrNotify& data_or_notify = impl_->data_or_notify_[i];
      if (data_or_notify.service_notify_ != NULL && data_or_notify.service_data_ == NULL) {
        return false;
      }
    }
    return true;
  } else {
    return false;
  }
}

ReturnCode RouteInfoNotify::WaitData(timespec& ts) {
  for (int i = 0; i < kDataOrNotifySize; ++i) {
    ServiceDataOrNotify& data_or_notify = impl_->data_or_notify_[i];
    if (data_or_notify.service_notify_ != NULL &&
        (data_or_notify.service_data_ == NULL ||
         data_or_notify.service_data_->GetDataStatus() == kDataInitFromDisk) &&
        data_or_notify.service_notify_->WaitDataWithRefUtil(ts, data_or_notify.service_data_) !=
            kReturnOk) {
      return kReturnTimeout;
    }
  }
  impl_->all_data_ready_ = true;
  return kReturnOk;
}

ReturnCode RouteInfoNotify::SetDataToRouteInfo(RouteInfo& route_info) {
  if (impl_->data_or_notify_[0].service_notify_ != NULL &&
      impl_->data_or_notify_[0].service_data_ != NULL) {
    // 检查是否服务不存在
    if (impl_->data_or_notify_[0].service_data_->GetDataStatus() == kDataNotFound) {
      POLARIS_LOG(LOG_ERROR, "discover instances for service[%s/%s] with service not found",
                  impl_->data_or_notify_[0].service_data_->GetServiceKey().namespace_.c_str(),
                  impl_->data_or_notify_[0].service_data_->GetServiceKey().name_.c_str());
      return kReturnServiceNotFound;
    }
    route_info.SetServiceInstances(new ServiceInstances(impl_->data_or_notify_[0].service_data_));
    impl_->data_or_notify_[0].service_data_ = NULL;
  }
  if (impl_->data_or_notify_[1].service_notify_ != NULL &&
      impl_->data_or_notify_[1].service_data_ != NULL) {
    if (impl_->data_or_notify_[1].service_data_->GetDataStatus() == kDataNotFound) {
      POLARIS_LOG(LOG_ERROR, "discover route rule for service[%s/%s] with service not found",
                  impl_->data_or_notify_[1].service_data_->GetServiceKey().namespace_.c_str(),
                  impl_->data_or_notify_[1].service_data_->GetServiceKey().name_.c_str());
      return kReturnServiceNotFound;
    }
    route_info.SetServiceRouteRule(new ServiceRouteRule(impl_->data_or_notify_[1].service_data_));
    impl_->data_or_notify_[1].service_data_ = NULL;
  }
  if (impl_->data_or_notify_[2].service_notify_ != NULL &&
      impl_->data_or_notify_[2].service_data_ != NULL) {
    if (impl_->data_or_notify_[2].service_data_->GetDataStatus() == kDataNotFound) {
      POLARIS_LOG(LOG_ERROR, "discover route rule for source service[%s/%s] with service not found",
                  impl_->data_or_notify_[2].service_data_->GetServiceKey().namespace_.c_str(),
                  impl_->data_or_notify_[2].service_data_->GetServiceKey().name_.c_str());
      return kReturnServiceNotFound;
    }
    route_info.SetSourceServiceRouteRule(
        new ServiceRouteRule(impl_->data_or_notify_[2].service_data_));
    impl_->data_or_notify_[2].service_data_ = NULL;
  }
  return kReturnOk;
}

ServiceRouterChain::ServiceRouterChain(const ServiceKey& service_key) {
  impl_                         = new ServiceRouterChainImpl();
  impl_->service_key_           = service_key;
  impl_->enable_                = false;
  impl_->is_rule_router_enable_ = false;
  impl_->context_               = NULL;
}

ServiceRouterChain::~ServiceRouterChain() {
  if (impl_ != NULL) {
    for (std::size_t i = 0; i < impl_->service_router_list_.size(); i++) {
      delete impl_->service_router_list_[i];
    }
    impl_->service_router_list_.clear();
    impl_->context_ = NULL;
    delete impl_;
  }
}

ReturnCode ServiceRouterChain::Init(Config* config, Context* context) {
  impl_->context_ = context;
  impl_->enable_  = config->GetBoolOrDefault(ServiceRouterConfig::kChainEnableKey,
                                            ServiceRouterConfig::kChainEnableDefault);
  if (impl_->enable_ == false) {
    POLARIS_LOG(LOG_INFO, "service router for service[%s/%s] is disable",
                impl_->service_key_.namespace_.c_str(), impl_->service_key_.name_.c_str());
    return kReturnOk;
  }

  impl_->plugin_name_list_ = config->GetListOrDefault(ServiceRouterConfig::kChainPluginListKey,
                                                      ServiceRouterConfig::kChainPluginListDefault);
  if (impl_->plugin_name_list_.empty()) {
    POLARIS_LOG(
        LOG_ERROR,
        "router chain for service[%s/%s] config[enable] is true, but config[chain] is error",
        impl_->service_key_.namespace_.c_str(), impl_->service_key_.name_.c_str());
    return kReturnInvalidConfig;
  }

  Config* chain_config          = config->GetSubConfig("plugin");
  Plugin* plugin                = NULL;
  ServiceRouter* service_router = NULL;
  ReturnCode ret                = kReturnOk;
  for (size_t i = 0; i < impl_->plugin_name_list_.size(); i++) {
    std::string& plugin_name = impl_->plugin_name_list_[i];
    // ServiceRouter别名转换,兼容老版本sdk
    if (plugin_name.compare(kPluginRuleServiceRouterAlias) == 0) {
      plugin_name = kPluginRuleServiceRouter;
    }
    if (plugin_name.compare(kPluginNearbyServiceRouterAlias) == 0) {
      plugin_name = kPluginNearbyServiceRouter;
    }

    if ((ret = PluginManager::Instance().GetPlugin(plugin_name, kPluginServiceRouter, plugin)) !=
        kReturnOk) {
      POLARIS_LOG(LOG_ERROR, "service router plugin with name[%s] for service[%s/%s] not found",
                  plugin_name.c_str(), impl_->service_key_.namespace_.c_str(),
                  impl_->service_key_.name_.c_str());
      break;
    }
    if ((service_router = dynamic_cast<ServiceRouter*>(plugin)) == NULL) {
      POLARIS_LOG(LOG_ERROR,
                  "plugin with name[%s] and type[%s] for service[%s/%s] can not "
                  "convert to service router",
                  plugin_name.c_str(), PluginTypeToString(kPluginServiceRouter),
                  impl_->service_key_.namespace_.c_str(), impl_->service_key_.name_.c_str());
      delete plugin;
      ret = kReturnInvalidConfig;
      break;
    }
    Config* plugin_config = chain_config->GetSubConfig(plugin_name);
    ret                   = service_router->Init(plugin_config, context);
    delete plugin_config;
    if (ret != kReturnOk) {
      POLARIS_LOG(LOG_ERROR, "init service router plugin[%s] for service[%s/%s] failed",
                  plugin_name.c_str(), impl_->service_key_.namespace_.c_str(),
                  impl_->service_key_.name_.c_str());
      delete service_router;
      break;
    }
    impl_->service_router_list_.push_back(service_router);
    if (plugin_name.compare(kPluginRuleServiceRouter) == 0) {
      impl_->is_rule_router_enable_ = true;
    }
  }
  delete chain_config;
  if (ret == kReturnOk) {
    POLARIS_LOG(LOG_INFO, "init service router plugin[%s] for service[%s/%s] success",
                StringUtils::JoinString(impl_->plugin_name_list_).c_str(),
                impl_->service_key_.namespace_.c_str(), impl_->service_key_.name_.c_str());
  }
  return ret;
}

ReturnCode ServiceRouterChain::DoRoute(RouteInfo& route_info, RouteResult* route_result) {
  ReturnCode ret;
  POLARIS_CHECK_ARGUMENT(route_result != NULL);
  bool need_trans_result = false;
  for (std::size_t i = 0; i < impl_->service_router_list_.size(); i++) {
    if (POLARIS_UNLIKELY(!route_info.IsRouterEnable(impl_->plugin_name_list_[i].c_str()))) {
      continue;
    }

    if (need_trans_result) {
      // 如果还有服务路由插件需要执行，则将这次执行结果设置到Context中给下一个插件执行
      route_info.UpdateServiceInstances(route_result->GetAndClearServiceInstances());
    }

    if ((ret = impl_->service_router_list_[i]->DoRoute(route_info, route_result)) != kReturnOk) {
      POLARIS_LOG(LOG_ERROR, "run service router plugin[%s] for service[%s/%s] return error[%s]",
                  impl_->plugin_name_list_[i].c_str(), impl_->service_key_.namespace_.c_str(),
                  impl_->service_key_.name_.c_str(), ReturnCodeToMsg(ret).c_str());
      if (ret == kReturnRouteRuleNotMatch) {
        POLARIS_LOG(
            LOG_ERROR, "router not match with instances[%s], route[%s], source route[%s]",
            route_info.GetServiceInstances()->GetServiceData()->ToJsonString().c_str(),
            route_info.GetServiceRouteRule()->GetServiceData()->ToJsonString().c_str(),
            route_info.GetSourceServiceRouteRule() == NULL
                ? "not use"
                : route_info.GetSourceServiceRouteRule()->GetServiceData()->ToJsonString().c_str());
      }
      return ret;
    }
    if (route_result->isRedirect()) {  // 需要转发，提前退出
      return kReturnOk;
    }

    // route result 有值了，下次需要往下传
    need_trans_result = true;
  }
  return kReturnOk;
}

void ServiceRouterChain::CollectStat(ServiceKey& service_key,
                                     std::map<std::string, RouterStatData*>& stat_data) {
  service_key = impl_->service_key_;
  for (std::size_t i = 0; i < impl_->service_router_list_.size(); i++) {
    RouterStatData* data = impl_->service_router_list_[i]->CollectStat();
    if (data != NULL) {
      data->record_.set_plugin_name(impl_->plugin_name_list_[i]);
      stat_data[impl_->plugin_name_list_[i]] = data;
    }
  }
}

RouteInfoNotify* ServiceRouterChain::PrepareRouteInfoWithNotify(RouteInfo& route_info) {
  const ServiceKey& service_key               = route_info.GetServiceKey();
  LocalRegistry* local_registry               = impl_->context_->GetLocalRegistry();
  RouteInfoNotifyImpl* route_info_notify_impl = NULL;
  ServiceData* service_data                   = NULL;

  // 服务实例
  ReturnCode ret =
      local_registry->GetServiceDataWithRef(service_key, kServiceDataInstances, service_data);
  if (ret == kReturnOk && service_data->GetDataStatus() != kDataNotFound) {
    route_info.SetServiceInstances(new ServiceInstances(service_data));
  } else {
    route_info_notify_impl = new RouteInfoNotifyImpl();
    local_registry->LoadServiceDataWithNotify(
        service_key, kServiceDataInstances, service_data,
        route_info_notify_impl->data_or_notify_[0].service_notify_);
    route_info_notify_impl->data_or_notify_[0].service_data_ = service_data;  // 可能有磁盘数据
  }
  if (impl_->is_rule_router_enable_) {
    // 服务路由
    service_data = NULL;
    ret = local_registry->GetServiceDataWithRef(service_key, kServiceDataRouteRule, service_data);
    if (ret == kReturnOk && service_data->GetDataStatus() != kDataNotFound) {
      route_info.SetServiceRouteRule(new ServiceRouteRule(service_data));
    } else {
      if (route_info_notify_impl == NULL) {
        route_info_notify_impl = new RouteInfoNotifyImpl();
      }
      local_registry->LoadServiceDataWithNotify(
          service_key, kServiceDataRouteRule, service_data,
          route_info_notify_impl->data_or_notify_[1].service_notify_);
      route_info_notify_impl->data_or_notify_[1].service_data_ = service_data;
    }
    ServiceInfo* source_service_info = route_info.GetSourceServiceInfo();
    if (source_service_info != NULL && !source_service_info->service_key_.name_.empty()) {
      // 源服务路由
      service_data = NULL;
      ret          = local_registry->GetServiceDataWithRef(source_service_info->service_key_,
                                                  kServiceDataRouteRule, service_data);
      if (ret == kReturnOk && service_data->GetDataStatus() != kDataNotFound) {
        route_info.SetSourceServiceRouteRule(new ServiceRouteRule(service_data));
      } else {
        if (route_info_notify_impl == NULL) {
          route_info_notify_impl = new RouteInfoNotifyImpl();
        }
        local_registry->LoadServiceDataWithNotify(
            source_service_info->service_key_, kServiceDataRouteRule, service_data,
            route_info_notify_impl->data_or_notify_[2].service_notify_);
        route_info_notify_impl->data_or_notify_[2].service_data_ = service_data;
      }
    }
  }
  return route_info_notify_impl == NULL ? NULL : new RouteInfoNotify(route_info_notify_impl);
}

bool ServiceRouterChain::IsRuleRouterEnable() { return impl_->is_rule_router_enable_; }

ReturnCode ServiceRouterChain::PrepareRouteInfo(RouteInfo& route_info, uint64_t timeout) {
  RouteInfoNotify* route_info_notify = PrepareRouteInfoWithNotify(route_info);
  if (route_info_notify == NULL) {
    return kReturnOk;
  }
  bool use_disk_data = false;
  if (!route_info_notify->IsDataReady(use_disk_data)) {
    timespec ts    = Time::CurrentTimeAddWith(timeout);
    ReturnCode ret = route_info_notify->WaitData(ts);
    if (ret == kReturnTimeout) {
      use_disk_data = true;
      if (!route_info_notify->IsDataReady(use_disk_data)) {
        delete route_info_notify;
        route_info_notify = NULL;
        return kReturnTimeout;
      }
    }
  }
  ReturnCode ret_code = route_info_notify->SetDataToRouteInfo(route_info);
  delete route_info_notify;
  route_info_notify = NULL;
  return ret_code;
}

void CalculateUnhealthySet(RouteInfo& route_info, ServiceInstances* service_instances,
                           std::set<Instance*>& unhealthy_set) {
  if (!route_info.IsIncludeUnhealthyInstances()) {
    unhealthy_set = service_instances->GetUnhealthyInstances();
  }
  std::map<std::string, Instance*>& instances = service_instances->GetInstances();
  if (!route_info.IsIncludeCircuitBreakerInstances()) {
    if (service_instances->GetService() == NULL) {
      POLARIS_LOG(LOG_ERROR, "Service member of %s:%s is null",
                  route_info.GetServiceKey().namespace_.c_str(),
                  route_info.GetServiceKey().name_.c_str());
      return;
    }
    std::set<std::string> circuit_breaker_set =
        service_instances->GetService()->GetCircuitBreakerOpenInstances();
    for (std::set<std::string>::iterator it = circuit_breaker_set.begin();
         it != circuit_breaker_set.end(); ++it) {
      std::map<std::string, Instance*>::iterator instance_it = instances.find(*it);
      if (instance_it != instances.end()) {
        unhealthy_set.insert(instance_it->second);
      }
    }
  }
}

}  // namespace polaris
