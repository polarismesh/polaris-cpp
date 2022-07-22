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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_ROUTE_INFO_H_
#define POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_ROUTE_INFO_H_

#include <set>
#include <string>
#include <vector>

#include "model/service_route_rule.h"
#include "polaris/defs.h"
#include "polaris/plugin.h"

namespace polaris {

// 路由插件链入参
class RouteInfo : Noncopyable {
 public:
  /// @brief 根据用户请求构造路由执行信息
  ///
  /// @param service_key 被调服务
  /// @param source_service_info 主调服务及相关metadata。不需要设置则传入NULL
  RouteInfo(const ServiceKey& service_key, ServiceInfo* source_service_info);

  /// @brief 根据tRPC框架请求构造路由执行信息
  ///
  /// @param service_key 被调服务
  /// @param source_service_info 主调服务及相关metadata。不需要设置则传入NULL
  /// @param source_service_data 主调服务的规则路由ServiceData。不需要设置则传入NULL
  RouteInfo(const ServiceKey& service_key, ServiceInfo* source_service_info, ServiceData* source_service_data);

  /// @brief 析构路由执行信息
  /// @note 此方法会析构所有设置的入参
  ~RouteInfo();

  /// @brief 设置路由插件链执行需要的被调服务实例数据
  ///
  /// @note 路由插件链执行前必须设置被调服务实例
  /// @param service_instances 被调服务实例
  void SetServiceInstances(ServiceInstances* service_instances) { service_instances_ = service_instances; }

  /// @brief 设置路由插件链执行需要的被调服务路由数据
  ///
  /// @note 如果路由插件链开启了规则路由，则在执行前必须设置被调服务路由规则
  /// @param service_route_rule 被调服务路由规则
  void SetServiceRouteRule(ServiceRouteRule* service_route_rule) { service_route_rule_ = service_route_rule; }

  /// @brief 设置路由插件链执行需要的主调服务路由数据
  ///
  /// @note 如果路由插件开启了规则路由，且用户传入了主调服务信息，则在执行前必须设置主调服务路由规则
  /// @param source_service_route_rule 主调服务路由规则
  void SetSourceServiceRouteRule(ServiceRouteRule* source_service_route_rule) {
    source_service_route_rule_ = source_service_route_rule;
  }

  /// @brief 获取被调服务
  ///
  /// @return const ServiceKey& 被调服务
  const ServiceKey& GetServiceKey() const { return service_key_; }

  /// @brief 获取主调服务
  ///
  /// @return ServiceInfo* 主调服务信息
  ServiceInfo* GetSourceServiceInfo() const { return source_service_info_; }

  /// @brief 获取tRPC框架传入的主调规则路由ServiceData
  ///
  /// @return ServiceData* 主调规则路由ServiceData
  ServiceData* GetSourceServiceData() const { return source_service_data_; }

  /// @brief 获取本次路由执行的服务实例
  ///
  /// @return ServiceInstances* 被调服务实例
  ServiceInstances* GetServiceInstances() const { return service_instances_; }

  /// @brief 获取被调路由规则
  ///
  /// @return ServiceRouteRule* 被调路由规则
  ServiceRouteRule* GetServiceRouteRule() const { return service_route_rule_; }

  /// @brief 获取主调路由规则
  ///
  /// @return ServiceRouteRule* 主调路由规则
  ServiceRouteRule* GetSourceServiceRouteRule() const { return source_service_route_rule_; }

  /// @brief 设置是否包含不健康实例，默认不包含
  void SetIncludeUnhealthyInstances();

  /// @brief 设置是否包含熔断实例，默认不包含
  void SetIncludeCircuitBreakerInstances();

  /// @brief 路由结果是否包含不健康实例
  bool IsIncludeUnhealthyInstances() const;

  /// @brief 路由结果是否包含熔断实例
  bool IsIncludeCircuitBreakerInstances() const;

  /// @brief 路由结果标志是否包含不健康和熔断实例
  uint8_t GetRequestFlags() const { return request_flag_; }

  void SetRequestFlags(uint64_t flag) { request_flag_ = flag; }

  /// @brief 设置是否执行就近路由
  void SetNearbyRouterDisable(bool value) { nearby_disable_ = value; }

  /// @brief 获取是否执行就近路由
  bool IsNearbyRouterDisable() const { return nearby_disable_; }

  /// @brief 设置请求标签信息
  void SetLables(const std::map<std::string, std::string>& labels);

  /// @brief 获取请求标签信息
  const std::map<std::string, std::string>& GetLabels() const;

  /// @brief 设置请求元数据路由参数
  void SetMetadataPara(const MetadataRouterParam& metadata_param);

  /// @brief 获取请求元数据
  const std::map<std::string, std::string>& GetMetadata() const;

  /// @brief 获取元数据路由降级类型
  MetadataFailoverType GetMetadataFailoverType() const;

  const std::string* GetCallerSetName() const;

  const std::string* GetCanaryName() const;

  void CalculateUnhealthySet(std::set<Instance*>& unhealthy_set);

  void SetCircuitBreakerVersion(uint64_t circuit_breaker_version) {
    circuit_breaker_version_ = circuit_breaker_version;
  }

  uint64_t GetCircuitBreakerVersion() const { return circuit_breaker_version_; }

 private:
  const ServiceKey& service_key_;
  ServiceInfo* source_service_info_;
  // tRPC框架使用，透传主调的规则路由ServiceData
  ServiceData* source_service_data_;

  ServiceInstances* service_instances_;
  ServiceRouteRule* service_route_rule_;
  ServiceRouteRule* source_service_route_rule_;
  uint8_t request_flag_;
  bool nearby_disable_;  // 表示不再执行就近路由

  const std::map<std::string, std::string>* labels_;
  const MetadataRouterParam* metadata_param_;
  uint64_t circuit_breaker_version_;
};

static const int kDataOrNotifySize = 3;

struct ServiceDataOrNotify {
  ServiceDataOrNotify() : service_data_(nullptr), service_notify_(nullptr) {}
  ~ServiceDataOrNotify() {
    if (service_data_ != nullptr) {
      service_data_->DecrementRef();
      service_data_ = nullptr;
    }
  }

  ServiceData* service_data_;
  ServiceDataNotify* service_notify_;
};

/// @brief 用于获取服务路由模块需要的数据
class RouteInfoNotify {
 public:
  /// @brief 创建对象，只能通过 @see ServiceRouterChain.PrepareRouteInfoWithNotify
  /// 方法在数据未准备就绪时创建
  RouteInfoNotify() : all_data_ready_(false) {}

  ~RouteInfoNotify() {}

  /// @brief 检查是否所有数据已经就绪，可控制是否使用磁盘数据
  ///
  /// @param use_disk_data 是否使用磁盘加载的数据
  /// @return true 数据就绪，可使用 @see SetDataToRouteInfo 方法设置到 @see RouteInfo 对象中
  /// @return false 数据未就绪
  bool IsDataReady(bool use_disk_data);

  /// @brief 等待服务路由所需要的数据就绪
  ///
  /// @param ts 时间对象，用于传入超时deadline
  /// @return ReturnCode kReturnOk：表示数据在超时前就绪
  ///                    kReturnTimeout：表示达到超时时间数据未就绪
  ReturnCode WaitData(timespec& ts);

  /// @brief 设置的服务数据到路由信息对象
  ///
  /// @param route_info
  /// @return ReturnCode kReturnOk：数据就绪且设置成功
  ///                    kReturnServiceNotFound：服务发现返回服务不存在
  ReturnCode SetDataToRouteInfo(RouteInfo& route_info);

 private:
  friend class ServiceRouterChain;
  bool all_data_ready_;
  ServiceDataOrNotify data_or_notify_[kDataOrNotifySize];
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_ROUTE_INFO_H_
