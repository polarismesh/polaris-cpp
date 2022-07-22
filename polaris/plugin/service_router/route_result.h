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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_ROUTE_RESULT_H_
#define POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_ROUTE_RESULT_H_

#include <set>
#include <string>
#include <vector>

#include "polaris/defs.h"
#include "polaris/plugin.h"

namespace polaris {

// 路由插件执行结果
class RouteResult : Noncopyable {
 public:
  /// @brief 构造路由执行结果对象
  RouteResult();

  /// @brief 析构路由执行结果对象
  ~RouteResult();

  /// @brief 路由结果是否为需要转发
  ///
  /// @return true 需要转发，通过可获取转发服务
  /// @return false 无需转发，则可获取路由计算出的服务实例信息
  bool isRedirect() const { return redirect_service_key_ != nullptr; }

  /// @brief 获取需要转发到的新服务
  ///
  /// @return ServiceKey& 需要转发到的新服务
  const ServiceKey& GetRedirectService() const;

  /// @brief 设置路由结果为转发类型并设置需要转发的服务
  ///
  /// @param service 需要转发到的新服务
  void SetRedirectService(const ServiceKey& service);

  /// @brief 设置路由结果所属subset
  ///
  /// @param subset subset信息
  void SetSubset(const std::map<std::string, std::string>& subset);

  /// @brief 获取路由结果subset信息
  ///
  /// @return const std::map<std::string, std::string>& subset信息
  const std::map<std::string, std::string>& GetSubset() const;

  void SetNewInstancesSet() { new_instances_set_ = true; }

  bool NewInstancesSet() const { return new_instances_set_; }

 private:
  ServiceKey* redirect_service_key_;

  std::map<std::string, std::string>* subset_;
  bool new_instances_set_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_ROUTE_RESULT_H_
