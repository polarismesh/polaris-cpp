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

#ifndef POLARIS_CPP_INCLUDE_POLARIS_CONTEXT_H_
#define POLARIS_CPP_INCLUDE_POLARIS_CONTEXT_H_

#include <map>
#include <string>
#include <vector>

#include "polaris/config.h"
#include "polaris/defs.h"
#include "polaris/noncopyable.h"
#include "polaris/plugin.h"

namespace polaris {

/// @brief Context模式，用于控制Context对象的初始化及释放规则
enum ContextMode {
  kNotInitContext = 0,  // 创建未初始化
  kPrivateContext,      // 私有模式，是否API对象会析构Context
  kShareContext,        // 共享模式，析构API对象不释放Context，需显式释放Context
  kLimitContext,        // 限流模式，会创建限流线程，并校验限流配置
  kShareContextWithoutEngine  // 共享模式，只初始化插件，不创建执行引擎
};

/// @brief SDK API 模式
///
/// 通过配置文件指定API的运行模式
enum ApiMode {
  kServerApiMode,  ///< Server模式，SDK与Server交互，SDK运行全部逻辑
  kAgentApiMode  ///< Agent模式，SDK与Agent交互，SDK只运行部分逻辑，由Agent运行一部分逻辑
};

class RouteInfoNotifyImpl;
/// @brief 用于获取服务路由模块需要的数据
class RouteInfoNotify {
public:
  /// @brief 创建对象，只能通过 @see ServiceRouterChain.PrepareRouteInfoWithNotify
  /// 方法在数据未准备就绪时创建
  explicit RouteInfoNotify(RouteInfoNotifyImpl* impl);
  ~RouteInfoNotify();

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
  RouteInfoNotifyImpl* impl_;
};

class ServiceRouterChainImpl;
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
  ServiceRouterChainImpl* impl_;
};

class CircuitBreakerChain {
public:
  virtual ~CircuitBreakerChain() {}

  virtual ReturnCode Init(Config* config, Context* context) = 0;

  virtual ReturnCode RealTimeCircuitBreak(const InstanceGauge& instance_gauge) = 0;

  virtual ReturnCode TimingCircuitBreak() = 0;

  virtual std::vector<CircuitBreaker*> GetCircuitBreakers() = 0;

  virtual ReturnCode TranslateStatus(const std::string& instance_id,
                                     CircuitBreakerStatus from_status,
                                     CircuitBreakerStatus to_status) = 0;

  virtual void PrepareServicePbConfTrigger() = 0;
};

class HealthCheckerChain {
public:
  virtual ~HealthCheckerChain() {}

  virtual ReturnCode Init(Config* config, Context* context) = 0;

  virtual ReturnCode DetectInstance(CircuitBreakerChain& circuit_breaker_chain) = 0;

  virtual std::vector<HealthChecker*> GetHealthCheckers() = 0;
};

class ServiceContextImpl;
/// @brief 服务级别上下文
class ServiceContext : public ServiceBase {
public:
  explicit ServiceContext(ServiceContextImpl* impl);
  ~ServiceContext();

  LoadBalancer* GetLoadBalancer(LoadBalanceType load_balance_type);

  WeightAdjuster* GetWeightAdjuster();

  CircuitBreakerChain* GetCircuitBreakerChain();

  HealthCheckerChain* GetHealthCheckerChain();

  ServiceRouterChain* GetServiceRouterChain();

  ServiceContextImpl* GetServiceContextImpl();

private:
  ServiceContextImpl* impl_;
};

class ContextImpl;
class Context : Noncopyable {
public:
  ~Context();

  ContextMode GetContextMode();

  ApiMode GetApiMode();

  /// @brief 获取或创建服务级别的上下文对象
  ///
  /// @param service_key 需要创建服务对象的服务
  /// @return ServiceContext* 获取或创建成功：返回之前创建或新创建的服务对象
  ///                         创建失败：返回NULL，通过日志查看创建失败的原因
  ServiceContext* GetOrCreateServiceContext(const ServiceKey& service_key);

  ServerConnector* GetServerConnector();

  LocalRegistry* GetLocalRegistry();

  ContextImpl* GetContextImpl();

  /// @brief 创建上下文对象
  ///
  /// @param config 配置对象
  /// @param mode 上下文模式，默认共享模式。 @see ContextMode
  /// @return Context* 创建成功：返回创建的上下文对象
  ///                  创建失败：返回NULL，可通过日志查看创建失败的原因
  static Context* Create(Config* config, ContextMode mode = kShareContext);

private:
  explicit Context(ContextImpl* impl);

  ContextImpl* impl_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_CONTEXT_H_
