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

#include "polaris/defs.h"
#include "polaris/plugin.h"

#include "plugin/service_router/route_info.h"
#include "plugin/service_router/route_result.h"
#include "v1/request.pb.h"

namespace polaris {

struct RouterStatData {
  v1::RouteRecord record_;
};

/// @brief 服务路由插件接口定义
class ServiceRouter : public Plugin {
 public:
  /// @brief 析构函数
  virtual ~ServiceRouter() {}

  /// @brief 通过配置进行初始化
  virtual ReturnCode Init(Config* config, Context* context) = 0;

  /// @brief 执行服务路由
  ///
  /// @param router_context 路由上限文，作为路由的输入
  /// @param router_result 路由结果，作为路由的输出
  /// @return ReturnCode
  virtual ReturnCode DoRoute(RouteInfo& route_info, RouteResult* route_result) = 0;

  /// @brief 收集路由统计数据
  virtual RouterStatData* CollectStat() = 0;

  /// @brief  返回路由插件的名称
  virtual std::string Name() = 0;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_SERVICE_ROUTER_H_
