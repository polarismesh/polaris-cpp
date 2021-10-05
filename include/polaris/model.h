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

/// @file model.h
/// @brief define the service model and service's data
///
#ifndef POLARIS_CPP_INCLUDE_POLARIS_MODEL_H_
#define POLARIS_CPP_INCLUDE_POLARIS_MODEL_H_

#include <string.h>
#include <time.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "polaris/defs.h"
#include "polaris/noncopyable.h"

namespace polaris {

///////////////////////////////////////////////////////////////////////////////
/// @brief 服务实例
///
/// 包含服务实例的所有信息
/// 其中实例动态权重由动态权重调整模块设置
/// 除此之外，其他属性均从服务端获取
class InstanceLocalValue;
class Instance {
public:
  Instance();

  Instance(const std::string& id, const std::string& host, const int& port, const uint32_t& weight);

  Instance(const Instance& other);

  const Instance& operator=(const Instance& other);

  ~Instance();

  std::string& GetId() const;  ///< 服务实例ID

  std::string& GetHost() const;  ///< 服务的节点IP或者域名

  int GetPort() const;  ///< 节点端口号

  uint64_t GetLocalId();  /// 本地生成的唯一ID

  std::string& GetVpcId();  ///< 获取服务实例所在VIP ID

  uint32_t GetWeight();  ///< 实例静态权重值, 0-1000

  std::string& GetProtocol();  ///< 实例协议信息

  std::string& GetVersion();  ///< 实例版本号信息

  int GetPriority();  ///< 实例优先级

  bool isHealthy();  ///< 实例健康状态

  bool isIsolate();  ///< 实例隔离状态

  std::map<std::string, std::string>& GetMetadata();  ///< 实例元数据信息

  std::string& GetContainerName();  ///< 实例元数据信息中的容器名

  std::string& GetInternalSetName();  ///< 实例元数据信息中的set名

  std::string& GetLogicSet();  ///< 实例LogicSet信息

  uint32_t GetDynamicWeight();  ///< 实例动态权重

  std::string& GetRegion();  ///< location region

  std::string& GetZone();  ///< location zone

  std::string& GetCampus();  ///< location campus

  uint64_t GetHash();

  InstanceLocalValue* GetLocalValue();

  uint64_t GetLocalityAwareInfo();  // locality_aware_info

private:
  friend class InstanceSetter;
  class InstanceImpl;
  InstanceImpl* impl;
};

///////////////////////////////////////////////////////////////////////////////
// 服务数据相关定义

/// @brief 服务数据类型
enum ServiceDataType {
  kServiceDataInstances,  ///< 服务实例数据
  kServiceDataRouteRule,  ///< 服务路由规则数据
  kServiceDataRateLimit,  ///< 服务限流规则数据
  kCircuitBreakerConfig,  /// < 熔断规则
};

/// @brief 服务数据状态
enum ServiceDataStatus {
  kDataNotInit = 0,
  kDataInitFromDisk,  ///< 表示服务数据从磁盘加载，向服务端更新服务失败时降级使用磁盘加载的数据
  kDataIsSyncing,  ///< 表示该服务数据时从服务器返回的，大于该值的数据都是从服务器端返回的
  kDataNotFound,   ///< 服务端返回未找到服务数据
};

class ServiceBaseImpl;
/// @brief 线程安全的引用计数基类
class ServiceBase : Noncopyable {
public:
  ServiceBase();

  virtual ~ServiceBase();

  /// @brief 原子增加一次引用计数
  void IncrementRef();

  /// @brief 原子减少一次引用计数
  void DecrementRef();

  /// @brief 原子减少一次引用计数并返回，主要用于测试
  uint64_t DecrementAndGetRef();

private:
  ServiceBaseImpl* impl_;
};

/// @brief 实例分组，用于记录路由计算的结果
class Selector;
class InstancesSetImpl;
class InstancesSet : public ServiceBase {
public:
  explicit InstancesSet(const std::vector<Instance*>& instances);

  InstancesSet(const std::vector<Instance*>& instances,
               const std::map<std::string, std::string>& subset);

  InstancesSet(const std::vector<Instance*>& instances,
               const std::map<std::string, std::string>& subset, const std::string& recover_info);

  virtual ~InstancesSet();

  const std::vector<Instance*>& GetInstances() const;

  const std::map<std::string, std::string>& GetSubset() const;

  const std::string& GetRecoverInfo() const;

  void SetSelector(Selector* selector);

  Selector* GetSelector();

  void AcquireSelectorCreationLock();

  void ReleaseSelectorCreationLock();

  InstancesSetImpl* GetInstancesSetImpl();

private:
  InstancesSetImpl* impl_;
};

class Service;
class ServiceDataImpl;

/// @brief 服务数据，作为 @ref Service 的属性，用于表示加载的各种类型的服务数据
///
/// ServiceData 一旦创建则不会修改。新版本的的数据创建成ServiceData对象后原子替换到Service中
/// 被替换的旧的同类型的服务数据会加入垃圾回收列表，垃圾回收使用引用计数方便判断是否可回收
class ServiceData : public ServiceBase {
public:
  virtual ~ServiceData();

  ServiceKey& GetServiceKey();

  const std::string& GetRevision() const;

  uint64_t GetCacheVersion();

  ServiceDataType GetDataType();

  ServiceDataStatus GetDataStatus();

  /// @brief 返回服务数据所属服务，只有更新到本地缓存的数据对象，才会关联对应的服务
  Service* GetService();

  const std::string& ToJsonString();

  ServiceDataImpl* GetServiceDataImpl();

  bool IsAvailable();

  static ServiceData* CreateFromJson(const std::string& content, ServiceDataStatus data_status,
                                     uint64_t available_time);

  static ServiceData* CreateFromPb(void* content, ServiceDataStatus data_status,
                                   uint64_t cache_version = 0);

private:
  static ServiceData* CreateFromPbJson(void* pb_content, const std::string& json_content,
                                       ServiceDataStatus data_status, uint64_t cache_version);

  explicit ServiceData(ServiceDataType data_type);
  ServiceDataImpl* impl_;
};

class ServiceDataNotifyImpl;
/// @brief 用于通知数据首次完成从服务器同步
class ServiceDataNotify : Noncopyable {
public:
  ServiceDataNotify(const ServiceKey& service_key, ServiceDataType data_type);

  ~ServiceDataNotify();

  bool hasData();

  /// @brief 通知回调
  ReturnCode WaitDataWithRefUtil(const timespec& ts, ServiceData*& service_data);

  void Notify(ServiceData* service_data);

private:
  ServiceDataNotifyImpl* impl_;
};

class ServiceInstancesImpl;
class InstancesSet;
/// @brief 服务实例数据：将类型为服务实例集合的服务数据封装成可选择的服务实例数据
class ServiceInstances : Noncopyable {
public:
  explicit ServiceInstances(ServiceData* service_data);
  ~ServiceInstances();

  /// @brief 获取服务metadata
  std::map<std::string, std::string>& GetServiceMetadata();

  /// @brief 获取所有服务实例列表
  std::map<std::string, Instance*>& GetInstances();

  /// @brief 获取不健康的实例列表
  std::set<Instance*>& GetUnhealthyInstances();

  /// @brief 获取半开实例数据
  void GetHalfOpenInstances(std::set<Instance*>& half_open_instances);

  /// @brief 获取可用的服务实例列表
  InstancesSet* GetAvailableInstances();

  /// @brief 保留部分实例
  void UpdateAvailableInstances(InstancesSet* available_instances);

  /// @brief 获取服务实例所属的服务
  Service* GetService();

  /// @brief 获取封装的服务数据
  ServiceData* GetServiceData();

  /// @brief 返回服务是否开启就近路由
  bool IsNearbyEnable();

  /// @brief 返回服务是否开启金丝雀路由
  bool IsCanaryEnable();

  /// @brief 返回隔离实例和权重为0的实例列表
  std::set<Instance*>& GetIsolateInstances();

private:
  ServiceInstancesImpl* impl_;
};

/// @brief 服务路由：封装类型为服务路由的服务数据提供服务路由接口
class ServiceRouteRule : Noncopyable {
public:
  explicit ServiceRouteRule(ServiceData* data);

  ~ServiceRouteRule();

  void* RouteRule();

  const std::set<std::string>& GetKeys() const;

  /// @brief 获取封装的服务数据
  ServiceData* GetServiceData();

private:
  ServiceData* service_data_;
};

///////////////////////////////////////////////////////////////////////////////
struct CircuitBreakerData;
struct DynamicWeightData;
struct CircuitBreakUnhealthySetsData;
struct SetCircuitBreakerUnhealthyInfo;

class ServiceImpl;
/// @brief 服务缓存
///
/// 用于在内存中管理服务数据，包含以下部分：
///     - 服务加载通知对象
///     - 服务端下发的服务数据：服务和服务下实例信息
///     - 动态权重调整数据：由动态权重插件更新
///     - 服务实例熔断数据：由熔断插件更新
///
/// 每个服务的服务缓存在 @ref LocalRegistry 插件中只有一个对象。从用户首次请求开始创建，
/// 到一定时间不再访问后过期删除，期间其他插件只会更新服务缓存中的数据。
class Service : Noncopyable {
public:
  Service(const ServiceKey& service_key, uint32_t service_id);
  ~Service();

  ServiceKey& GetServiceKey();

  /// @brief 将服务关联到服务数据
  ///
  /// @param service_data 服务数据
  void UpdateData(ServiceData* service_data);

  void SetDynamicWeightData(const DynamicWeightData& dynamic_weight_data);

  uint64_t GetDynamicWeightDataVersion();

  std::map<std::string, uint32_t> GetDynamicWeightData();

  void SetCircuitBreakerData(const CircuitBreakerData& circuit_breaker_data);

  uint64_t GetCircuitBreakerDataVersion();

  std::map<std::string, int> GetCircuitBreakerHalfOpenInstances();

  std::set<std::string> GetCircuitBreakerOpenInstances();

  ReturnCode TryChooseHalfOpenInstance(std::set<Instance*>& instances, Instance*& instance);

  ReturnCode WriteCircuitBreakerUnhealthySets(
      const CircuitBreakUnhealthySetsData& unhealthy_sets_data);

  uint64_t GetCircuitBreakerSetUnhealthyDataVersion();
  std::map<std::string, SetCircuitBreakerUnhealthyInfo> GetCircuitBreakerSetUnhealthySets();

private:
  ServiceImpl* impl_;
};

///////////////////////////////////////////////////////////////////////////////

class RouteInfoImpl;
// 路由插件链入参
class RouteInfo : Noncopyable {
public:
  /// @brief 根据用户请求构造路由执行信息
  ///
  /// @param service_key 被调服务
  /// @param source_service_info 主调服务及相关metadata。不需要设置则传入NULL
  RouteInfo(const ServiceKey& service_key, ServiceInfo* source_service_info);

  /// @brief 析构路由执行信息
  /// @note 此方法会析构所有设置的入参
  ~RouteInfo();

  /// @brief 设置路由插件链执行需要的被调服务实例数据
  ///
  /// @note 路由插件链执行前必须设置被调服务实例
  /// @param service_instances 被调服务实例
  void SetServiceInstances(ServiceInstances* service_instances);

  /// @brief 设置路由插件链执行需要的被调服务路由数据
  ///
  /// @note 如果路由插件链开启了规则路由，则在执行前必须设置被调服务路由规则
  /// @param service_route_rule 被调服务路由规则
  void SetServiceRouteRule(ServiceRouteRule* service_route_rule);

  /// @brief 设置路由插件链执行需要的主调服务路由数据
  ///
  /// @note 如果路由插件开启了规则路由，且用户传入了主调服务信息，则在执行前必须设置主调服务路由规则
  /// @param source_service_route_rule 主调服务路由规则
  void SetSourceServiceRouteRule(ServiceRouteRule* source_service_route_rule);

  /// @brief 更新路由插件所需的被调服务实例数据
  ///
  /// 该方法在路由插件链执行过程中调用，将上一个插件执行结果设置给下一个插件使用
  /// 并释放上一个插件使用的服务实例
  /// @param service_instances 新的被调服务实例
  void UpdateServiceInstances(ServiceInstances* service_instances);

  /// @brief 获取被调服务
  ///
  /// @return const ServiceKey& 被调服务
  const ServiceKey& GetServiceKey();

  /// @brief 获取主调服务
  ///
  /// @return ServiceInfo* 主调服务信息
  ServiceInfo* GetSourceServiceInfo();

  /// @brief 获取本次路由执行的服务实例
  ///
  /// @return ServiceInstances* 被调服务实例
  ServiceInstances* GetServiceInstances();

  /// @brief 获取被调路由规则
  ///
  /// @return ServiceRouteRule* 被调路由规则
  ServiceRouteRule* GetServiceRouteRule();

  /// @brief 获取主调路由规则
  ///
  /// @return ServiceRouteRule* 主调路由规则
  ServiceRouteRule* GetSourceServiceRouteRule();

  /// @brief 设置是否包含不健康实例，默认不包含
  void SetIncludeUnhealthyInstances();

  /// @brief 设置是否包含熔断实例，默认不包含
  void SetIncludeCircuitBreakerInstances();

  /// @brief 路由结果是否包含不健康实例
  bool IsIncludeUnhealthyInstances();

  /// @brief 路由结果是否包含熔断实例
  bool IsIncludeCircuitBreakerInstances();

  /// @brief 路由结果标志是否包含不健康和熔断实例
  uint8_t GetRequestFlags();

  /// @brief 设置是否启用指定路由插件
  ///
  /// @param router_name 路由插件名字
  /// @param enable true为开启，false为关闭
  void SetRouterFlag(const char* router_name, bool enable);

  /// @brief 设置结束执行路由链中其他路由插件
  void SetRouterChainEnd(bool value);

  /// @brief 获取是否结束路由链标志
  bool IsRouterChainEnd();

  /// @brief 查询路由插件是否启用
  bool IsRouterEnable(const char* router_name);

  /// @brief 设置请求标签信息
  void SetLables(const std::map<std::string, std::string>& lables);

  /// @brief 获取请求标签信息
  const std::map<std::string, std::string>& GetLabels();

  /// @brief 设置请求元数据路由参数
  void SetMetadataPara(const MetadataRouterParam& metadata_param);

  /// @brief 获取请求元数据
  const std::map<std::string, std::string>& GetMetadata();

  /// @brief 获取元数据路由降级类型
  MetadataFailoverType GetMetadataFailoverType();

private:
  ServiceKey service_key_;
  ServiceInfo* source_service_info_;

  ServiceInstances* service_instances_;
  ServiceRouteRule* service_route_rule_;
  ServiceRouteRule* source_service_route_rule_;
  uint8_t route_flag_;

  /// @brief const char* 比较函数
  struct less_for_c_strings {
    bool operator()(const char* p1, const char* p2) const { return strcmp(p1, p2) < 0; }
  };
  std::set<const char*, less_for_c_strings>* disable_routers_;

  bool end_route_;  // 表示不再执行后续的路由插件

  std::map<std::string, std::string>* labels_;
  MetadataRouterParam* metadata_param_;
};

class RouteResultImpl;
// 路由插件执行结果
class RouteResult : Noncopyable {
public:
  /// @brief 构造路由执行结果对象
  RouteResult();

  /// @brief 析构路由执行结果对象
  ~RouteResult();

  /// @brief 设置路由结果为服务实例类型并设置路由计算得到的服务实例
  ///
  /// @param service_instances 服务实例
  void SetServiceInstances(ServiceInstances* service_instances);

  /// @brief 获取路由计算出的服务实例信息
  ///
  /// @return ServiceInstances* 路由计算出的服务实例信息
  ServiceInstances* GetServiceInstances();

  /// @brief 获取服务实例结果并清空服务路由结果
  ///
  /// @return ServiceInstances* 路由计算出的服务实例信息
  ServiceInstances* GetAndClearServiceInstances();

  /// @brief 路由结果是否为需要转发
  ///
  /// @return true 需要转发，通过可获取转发服务
  /// @return false 无需转发，则可获取路由计算出的服务实例信息
  bool isRedirect();

  /// @brief 获取需要转发到的新服务
  ///
  /// @return ServiceKey& 需要转发到的新服务
  const ServiceKey& GetRedirectService();

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
  const std::map<std::string, std::string>& GetSubset();

private:
  ServiceInstances* service_instances_;
  ServiceKey* redirect_service_key_;

  std::map<std::string, std::string> subset_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_MODEL_H_
