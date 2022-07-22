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

/// @file plugin.h
/// @brief this file define all plugin interface
///
#ifndef POLARIS_CPP_INCLUDE_POLARIS_PLUGIN_H_
#define POLARIS_CPP_INCLUDE_POLARIS_PLUGIN_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "polaris/config.h"
#include "polaris/defs.h"
#include "polaris/model.h"

namespace polaris {

/// @brief 扩展点类型
///
/// 扩展点有两种级别：
///     1. API级别：根据每个API配置进行初始化
///     2. Service级别：根据每个服务配置进行初始化
enum PluginType {
  kPluginServerConnector,  ///< server代理扩展点
  kPluginLocalRegistry,    ///< 本地缓存扩展点
  kPluginServiceRouter,    ///< 服务路由扩展点
  kPluginLoadBalancer,     ///< 负载均衡扩展点
  kPluginHealthChecker,    ///< 健康探测扩展点
  kPluginCircuitBreaker,   ///< 节点熔断扩展点
  kPluginWeightAdjuster,   ///< 动态权重调整扩展点
  kPluginStatReporter,     ///< 统计上报扩展点
  kPluginAlertReporter,    ///< 告警扩展点
  kPluginServerMetric,     ///< SDK与Server请求结果统计

  kPluginTypeMaxCount  // 插件类型数量
};

/// @brief 路由插件事件回调
class InstancesData;
typedef void (*InstancePreUpdateHandler)(const InstancesData* oldInsts, InstancesData* newInsts);

/// @brief 路由插件事件类型
enum PluginEventType {
  kPluginEvtInstancePreUpdate = 100,       // 实例数据更新前
  kPluginEvtInstancePostUpdate = 101,      // 实例数据更新后
  kPluginEvtServiceRoutePreUpdate = 200,   // 服务路由数据更新前
  kPluginEvtServiceRoutePostUpdate = 201,  // 服务路由数据更新后
};

class Context;
class InstancesData;

/// @brief 扩展点接口
class Plugin {
 public:
  virtual ~Plugin() {}

  /// @brief 初始化插件
  ///
  /// @param config 配置信息
  /// @return ReturnCode 操作返回码
  virtual ReturnCode Init(Config* config, Context* context) = 0;
};

/// @brief 插件工厂方法函数指针
typedef Plugin* (*PluginFactory)();

/// @brief 注册插件
ReturnCode RegisterPlugin(std::string name, PluginType plugin_type, PluginFactory plugin_factory);

enum CircuitBreakerStatus {
  kCircuitBreakerClose = 0,
  kCircuitBreakerHalfOpen,
  kCircuitBreakerOpen,
  kCircuitBreakerPreserved,
};

struct CircuitBreakerData {
  uint64_t version;
  std::set<std::string> open_instances;
  std::map<std::string, int> half_open_instances;
};

struct SetCircuitBreakerUnhealthyInfo {
  // 只坑能是 熔断、保持、半开
  CircuitBreakerStatus status;
  float half_open_release_percent;
  uint64_t open_status_begin_time;
  uint64_t last_half_open_release_time;
};

struct CircuitBreakUnhealthySetsData {
  uint64_t version;
  std::map<std::string, SetCircuitBreakerUnhealthyInfo> subset_unhealthy_infos;
};

enum DynamicWeightDataStatus {
  kDynamicWeightNoInit = 0,
  kDynamicWeightUpdating = 1,
  kDynamicWeightInvalid = 2,
};

struct DynamicWeightData {
  uint64_t version;
  DynamicWeightDataStatus status;
  uint64_t sync_interval;
  std::map<std::string, uint32_t> dynamic_weights;
};

// 服务数据加载完成通知
class DataNotify {
 public:
  virtual ~DataNotify() {}

  // 通知服务数据加载完成
  virtual void Notify() = 0;

  // 等待服务数据加载完成
  virtual bool Wait(uint64_t timeout) = 0;
};

// 服务数据加载通知对象工厂方法
typedef DataNotify* (*DataNotifyFactory)();

// 设置服务数据加载通知对象工程方法
class ConsumerApi;
bool SetDataNotifyFactory(ConsumerApi* consumer, DataNotifyFactory factory);

/// @brief 扩展点接口：本地缓存扩展点
///
/// 数据的状态转换如下：
/// 初始状态为： InitFromDisk（使用磁盘数据初始化）和NotInit（刚创建未初始化）
/// Get方法获取到的数据可能是InitFromDisk状态。
/// 对于InitFromDisk和NotInit状态的数据首次访问时，需要向ServerConnector注册Handler
/// 并将状态转换为FirstAccessed，在ServerConnector中更新到数据后，将状态转换为IsSyncing
class LocalRegistry : public Plugin {
 public:
  /// @brief 析构函数
  virtual ~LocalRegistry() {}

  /// @brief 通过配置进行初始化
  virtual ReturnCode Init(Config* config, Context* context) = 0;

  virtual void RunGcTask() = 0;

  /// @brief 检查并删除过期服务数据
  ///
  /// @note 必须在函数内部删除并取消注册到ServerConnector的Handler
  /// 如果返回在外部删除，则可能删除过期后新的请求触发的handler
  virtual void RemoveExpireServiceData() = 0;

  /// @brief 非阻塞获取服务缓存，只读缓存中的信息
  ///
  /// @param service_name 服务名
  /// @param service_namespace 服务命名空间
  /// @param service 返回的服务缓存
  /// @return ReturnCode 调用返回码
  virtual ReturnCode GetServiceDataWithRef(const ServiceKey& service_key, ServiceDataType data_type,
                                           ServiceData*& service_data) = 0;

  /// @brief 非阻塞触发加载服务，并获取Notify用来等待服务数据首次更新
  ///
  /// 如果首次获取服务，则会返回码为kReturnNotInit，
  /// 表示需要向ServerConnector注册service_key对应的Handler
  /// @param service_name 服务名
  /// @param service_namespace 服务命名空间
  /// @param notify 缓存加载完毕时回调通知对象
  /// @return ReturnCode 调用返回码
  virtual ReturnCode LoadServiceDataWithNotify(const ServiceKey& service_key, ServiceDataType data_type,
                                               ServiceData*& service_data, ServiceDataNotify*& notify) = 0;

  virtual ReturnCode UpdateServiceData(const ServiceKey& service_key, ServiceDataType data_type,
                                       ServiceData* service_data) = 0;

  virtual ReturnCode UpdateServiceSyncTime(const ServiceKey& service_key, ServiceDataType data_type) = 0;

  virtual ReturnCode UpdateCircuitBreakerData(const ServiceKey& service_key,
                                              const CircuitBreakerData& circuit_breaker_data) = 0;

  virtual ReturnCode UpdateSetCircuitBreakerData(const ServiceKey& service_key,
                                                 const CircuitBreakUnhealthySetsData& unhealthy_sets) = 0;

  virtual ReturnCode GetCircuitBreakerInstances(const ServiceKey& service_key, ServiceData*& service_data,
                                                std::vector<Instance*>& open_instances) = 0;

  /// @brief 更新服务实例状态，properties存放的是状态值，当前支持2个key
  ///
  /// 1. ReadyToServe: 故障熔断标识，true or false
  /// 2. DynamicWeight：动态权重值
  /// @param instance_id 服务实例ID
  /// @param properties 属性信息
  /// @return ReturnCode 调用返回码
  virtual ReturnCode UpdateDynamicWeight(const ServiceKey& service_key,
                                         const DynamicWeightData& dynamic_weight_data) = 0;

  /// @brief 检测清除掉过期的动态权重数据
  virtual void CheckAndSetExpireDynamicWeightServiceData(const ServiceKey& service_key) = 0;

  // @brief 用于查看缓存中有多少个service的接口
  ///
  /// @param service_key_set 输出参数:用于存放ServiceKey信息
  /// @return ReturnCode 调用返回码
  virtual ReturnCode GetAllServiceKey(std::set<ServiceKey>& service_key_set) = 0;
};

/// @brief 扩展点接口：负载均衡
class LoadBalancer : public Plugin {
 public:
  /// @brief 析构函数
  virtual ~LoadBalancer() {}

  /// @brief 通过配置进行初始化
  virtual ReturnCode Init(Config* config, Context* context) = 0;

  /// @brief 获取负载均衡插件对应的类型
  virtual LoadBalanceType GetLoadBalanceType() = 0;

  /// @brief 通过负载均衡算法选择一个服务实例
  ///
  /// @param service 过滤后的服务缓存信息
  /// @param criteria 负载均衡信息
  /// @param instance 被选择的服务实例
  /// @return ReturnCode 调用返回码
  virtual ReturnCode ChooseInstance(ServiceInstances* instances, const Criteria& criteria, Instance*& instance) = 0;
};

/// @brief 调用统计结果上报
struct InstanceGauge {
  InstanceGauge()
      : call_ret_status(kCallRetOk),
        call_ret_code(0),
        call_daley(0),
        locality_aware_info(0),
        source_service_key(nullptr),
        subset_(nullptr),
        labels_(nullptr) {}

  ~InstanceGauge() {
    if (source_service_key != nullptr) {
      delete source_service_key;
      source_service_key = nullptr;
    }
    if (subset_ != nullptr) {
      delete subset_;
      subset_ = nullptr;
    }
    if (labels_ != nullptr) {
      delete labels_;
      labels_ = nullptr;
    }
  }

  ServiceKey service_key_;
  std::string instance_id;
  CallRetStatus call_ret_status;
  int call_ret_code;
  uint64_t call_daley;
  uint64_t locality_aware_info;

  ServiceKey* source_service_key;
  std::map<std::string, std::string>* subset_;
  std::map<std::string, std::string>* labels_;
};

/// @brief 探测结果
struct DetectResult {
  std::string detect_type;
  int return_code;
  uint64_t elapse;
};

/// @brief 扩展点接口：主动健康探测策略
class HealthChecker : public Plugin {
 public:
  /// @brief 析构函数
  virtual ~HealthChecker() {}

  /// @brief 通过配置进行初始化
  virtual ReturnCode Init(Config* config, Context* context) = 0;

  /// @brief  添加待探测节点
  ///
  /// @param instance 待探测节点
  /// @return ReturnCode 调用返回码
  virtual ReturnCode DetectInstance(Instance& instance, DetectResult& detect_result) = 0;
};

/// @brief 扩展点接口：上报统计结果
class StatReporter : public Plugin {
 public:
  /// @brief 析构函数
  virtual ~StatReporter() {}

  /// @brief 通过配置进行初始化
  virtual ReturnCode Init(Config* config, Context* context) = 0;

  /// @brief 执行统计数据上报
  ///
  /// @param stat 统计数据
  /// @return ReturnCode 执行结果
  virtual ReturnCode ReportStat(const InstanceGauge& instance_gauge) = 0;
};

/// @brief 告警级别
enum AlertLevel {
  kNormalAlert = 0,  ///< 普通告警
  kCriticalAlert,    ///< 严重告警
  kFatalAlert        ///< 致命告警
};

/// @brief 扩展点接口：上报告警信息
class AlertReporter : public Plugin {
 public:
  /// @brief 析构函数
  virtual ~AlertReporter() {}

  /// @brief 通过配置进行初始化
  virtual ReturnCode Init(Config* config, Context* context) = 0;

  /// @brief 执行告警上报
  ///
  /// @param alert_level 告警级别
  /// @param msg 告警消息内容
  /// @return ReturnCode 执行结果
  virtual ReturnCode ReportAlert(AlertLevel alert_level, std::string msg) = 0;
};

///@brief 扩展点接口：收集北极星SDK调用服务器结果
class ServerMetric : public Plugin {
 public:
  virtual ~ServerMetric() {}

  /// @brief 通过配置进行初始化
  virtual ReturnCode Init(Config* config, Context* context) = 0;

  /// @brief 内部服务调用结果上报
  ///
  /// @param service_key 服务
  /// @param instance 实例
  /// @param ret_code 返回码
  /// @param ret_status 是否成功
  /// @param daley 延迟
  virtual void MetricReport(const ServiceKey& service_key, const Instance& instance, ReturnCode ret_code,
                            CallRetStatus ret_status, uint64_t daley) = 0;
};

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_PLUGIN_H_
