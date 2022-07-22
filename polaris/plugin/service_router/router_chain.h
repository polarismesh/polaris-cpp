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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_ROUTER_CHAIN_H_
#define POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_ROUTER_CHAIN_H_

#include <string>
#include <vector>

#include "plugin/service_router/service_router.h"

namespace polaris {

class ServiceRouterChain {
 public:
  explicit ServiceRouterChain(const ServiceKey& service_key);
  ~ServiceRouterChain();

  ReturnCode Init(Config* config, Context* context);

  /// @brief 准备服务路由数据，如果数据未就绪，则创建通知对象用于异步等待服务数据
  ///
  /// @param route_info 服务路由数据对象
  /// @return RouteInfoNotify* 路由数据通知对象。如果为NULL则表示数据准备就绪
  RouteInfoNotify* PrepareRouteInfoWithNotify(RouteInfo& route_info);

  /// @brief 阻塞方式准备服务路由数据
  ///
  /// @param route_info 服务路由数据对象
  /// @param timeout 超时等待时间，用于控制该方法最多阻塞的时间
  /// @return ReturnCode kReturnOk：数据就绪
  ///                    kReturnTimeout：数据未就绪
  ReturnCode PrepareRouteInfo(RouteInfo& route_info, uint64_t timeout);

  /// @brief 返回规则路由插件是否开启
  bool IsRuleRouterEnable();

  /// @brief 执行服务路由链
  ///
  /// @param route_info 准备就绪的服务数据
  /// @param route_result 服务路由执行结果
  /// @return ReturnCode kReturnOk：执行成功
  ReturnCode DoRoute(RouteInfo& route_info, RouteResult* route_result);

  /// @brief 收集调用统计信息
  ///
  /// @param service_key 调用服务
  /// @param stat_data 调用统计信息
  void CollectStat(ServiceKey& service_key, std::map<std::string, RouterStatData*>& stat_data);

 private:
  ServiceData* PrepareServiceData(const ServiceKey& service_key, ServiceDataType data_type, int notify_index,
                                  RouteInfoNotify*& notify);

 private:
  Context* context_;
  ServiceKey service_key_;
  std::vector<ServiceRouter*> service_router_list_;
  std::vector<std::string> plugin_name_list_;
  bool is_rule_router_enable_;
  bool is_set_router_enable_;
  bool is_canary_router_enable_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_ROUTER_CHAIN_H_
