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

#ifndef POLARIS_CPP_INCLUDE_POLARIS_MODEL_H_
#define POLARIS_CPP_INCLUDE_POLARIS_MODEL_H_

#include <atomic>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "polaris/defs.h"
#include "polaris/instance.h"
#include "polaris/noncopyable.h"

namespace polaris {

// ---------------------------------------------------------------------------
// 服务数据相关定义

/// @brief 服务数据类型
enum ServiceDataType {
  kServiceDataInstances,      ///< 服务实例数据
  kServiceDataRouteRule,      ///< 服务路由规则数据
  kServiceDataRateLimit,      ///< 服务限流规则数据
  kCircuitBreakerConfig,      /// < 熔断规则
};

/// @brief 服务数据状态
enum ServiceDataStatus {
  kDataNotInit = 0,
  kDataInitFromDisk,  ///< 表示服务数据从磁盘加载，向服务端更新服务失败时降级使用磁盘加载的数据
  kDataIsSyncing,  ///< 表示该服务数据时从服务器返回的，大于该值的数据都是从服务器端返回的
  kDataNotFound,  ///< 服务端返回未找到服务数据
};

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
  std::atomic<std::uint32_t> ref_count_;
};

/// @brief 实例分组，用于记录路由计算的结果
class Selector;
class InstancesSetImpl;
class InstancesSet : public ServiceBase {
 public:
  explicit InstancesSet(const std::vector<Instance*>& instances);

  InstancesSet(const std::vector<Instance*>& instances, const std::map<std::string, std::string>& subset);

  InstancesSet(const std::vector<Instance*>& instances, const std::map<std::string, std::string>& subset,
               const std::string& recover_info);

  virtual ~InstancesSet();

  const std::vector<Instance*>& GetInstances() const;

  const std::map<std::string, std::string>& GetSubset() const;

  const std::string& GetRecoverInfo() const;

  void SetSelector(Selector* selector);

  Selector* GetSelector();

  InstancesSetImpl* GetImpl() const;

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

  const ServiceKey& GetServiceKey() const;

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

  static ServiceData* CreateFromPb(void* content, ServiceDataStatus data_status, uint64_t cache_version = 0);

 private:
  static ServiceData* CreateFromPbJson(void* pb_content, const std::string& json_content, ServiceDataStatus data_status,
                                       uint64_t cache_version);

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

  uint64_t GetDynamicWeightVersion() const;

  void SetTempDynamicWeightVersion(uint64_t new_dynamic_weight_version);

  void CommitDynamicWeightVersion(uint64_t dynamic_weight_version);

  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
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

  void SetDynamicWeightData(const DynamicWeightData& dynamic_weight_data, bool& states_change);

  bool CheckAndSetDynamicWeightExpire();

  uint64_t GetDynamicWeightDataVersion();

  std::map<std::string, uint32_t> GetDynamicWeightData();

  void SetCircuitBreakerData(const CircuitBreakerData& circuit_breaker_data);

  uint64_t GetCircuitBreakerDataVersion();

  std::map<std::string, int> GetCircuitBreakerHalfOpenInstances();

  std::set<std::string> GetCircuitBreakerOpenInstances();

  ReturnCode TryChooseHalfOpenInstance(std::set<Instance*>& instances, Instance*& instance);

  ReturnCode WriteCircuitBreakerUnhealthySets(const CircuitBreakUnhealthySetsData& unhealthy_sets_data);

  uint64_t GetCircuitBreakerSetUnhealthyDataVersion();
  std::map<std::string, SetCircuitBreakerUnhealthyInfo> GetCircuitBreakerSetUnhealthySets();

 private:
  ServiceImpl* impl_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_MODEL_H_
