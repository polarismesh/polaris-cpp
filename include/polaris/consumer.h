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

#ifndef POLARIS_CPP_INCLUDE_POLARIS_CONSUMER_H_
#define POLARIS_CPP_INCLUDE_POLARIS_CONSUMER_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "polaris/context.h"
#include "polaris/defs.h"
#include "polaris/noncopyable.h"

namespace polaris {

/// @brief 获取单个服务实例请求
class GetOneInstanceRequest : Noncopyable {
 public:
  /// @brief 构建获取单个服务实例请求对象
  ///
  /// @param service_key 命名空间和服务名
  explicit GetOneInstanceRequest(const ServiceKey& service_key);

  /// @brief 析构获取单个服务实例请求对象
  ~GetOneInstanceRequest();

  /// @brief 设置hash key，用于一致性哈希负载均衡算法选择服务实例。其他负载均衡算法不用设置
  void SetHashKey(uint64_t hash_key);

  /// @brief 设置 hash 字符串, sdk 会用 hash_string 算出一个 uint64_t
  /// 的哈希值用于一致性哈希负载均衡算法选择服务实例。其他负载均衡算法不用设置
  void SetHashString(const std::string& hash_string);

  /// @brief 设置是否略过跳过半开探测节点
  /// @note 只在重试业务时设置为true。如果一直设置为true，则熔断节点在网络探测成功后也一直无法恢复
  void SetIgnoreHalfOpen(bool ignore_half_open);

  /// @brief 设置源服务信息，用于服务路由计算。可选
  ///
  /// @param source_service 源服务信息，包括源服务命名空间和用于过滤的metadata
  void SetSourceService(const ServiceInfo& source_service);

  /// @brief 设置调用哪个set下的服务
  ///
  /// @param set_name 主调指定的被调服务的set名
  bool SetSourceSetName(const std::string& set_name);

  /// @brief 设置调用哪个金丝雀服务实例
  ///
  /// @param canary 主调指定的金丝雀
  void SetCanary(const std::string& canary);

  /// @brief 设置请求流水号。可选，默认随机流水号
  ///
  /// @param flow_id 用于跟踪请求的流水号
  void SetFlowId(uint64_t flow_id);

  /// @brief 设置请求超时时间。可选，默认为全局配置的API超时时间
  ///
  /// @param timeout 设置请求超时时间，可选，单位ms
  void SetTimeout(uint64_t timeout);

  /// @brief 设置请求标签，用于接口级别熔断
  ///
  /// @param labels 请求标签
  void SetLabels(const std::map<std::string, std::string>& labels);

  /// @brief 设置元数据，用于元数据路由
  ///
  /// @param metadata 元数据
  void SetMetadata(std::map<std::string, std::string>& metadata);

  /// @brief 设置元数据路由匹配失败时的降级策略，默认不降级
  ///
  /// @param metadata_failover_type 元数据路由降级策略
  void SetMetadataFailover(MetadataFailoverType metadata_failover_type);

  /// @brief 设置负载均衡类型。可选，默认使用配置文件中设置的类型
  ///
  /// @param load_balance_type 负载均衡类型
  void SetLoadBalanceType(LoadBalanceType load_balance_type);

  /// @brief 设置用于重试的实例数。可选，默认不返回用于重试的实例
  ///
  /// @note 第一个实例由负载均衡器给出，外加backup_instance_num个实例，实例不重复，但不保证数量
  ///       内部的一致性环hash负载均衡返回实例后方相邻的实例，其他返回随机实例
  ///       从GetOneInstance的InstancesResponse获取实例
  ///
  /// @param backup_instance_num 重试（备份）实例数
  void SetBackupInstanceNum(uint32_t backup_instance_num);

  /// @brief 用于一致性hash算法时获取副本实例
  ///
  /// @param replicate_index 副本索引，默认为0表示当前hash实例本身
  ///                        大于0表示从hash实例后面的第几个副本
  void SetReplicateIndex(int replicate_index);

  class Impl;
  Impl& GetImpl() const;

 private:
  Impl* impl_;
};

/// @brief 获取批量服务实例请求
class GetInstancesRequest : Noncopyable {
 public:
  /// @brief 构造获取批量服务实例请求
  ///
  /// @param service_key 命名空间和服务名
  explicit GetInstancesRequest(const ServiceKey& service_key);

  /// @brief 析构获取批量服务实例请求
  ~GetInstancesRequest();

  /// @brief 设置服务路由时否包含不健康的服务实例。可选，默认不包含
  ///
  /// @note 即使设置不包含的情况下仍然可能降级返回不健康实例
  /// @param include_unhealthy_instances 是否包含不健康服务实例
  void SetIncludeUnhealthyInstances(bool include_unhealthy_instances);

  /// @brief 设置服务路由时是否包含熔断的服务实例。可选，默认不包含。
  ///
  /// @note 即使设置不包含的情况下仍然可能降级返回熔断实例
  /// @param include_circuit_breaker_instances 是否包含熔断实例
  void SetIncludeCircuitBreakInstances(bool include_circuit_breaker_instances);

  /// @brief 设置是否跳过服务路由。可选，默认不跳过服务路由
  ///
  /// @param skip_route_filter 是否跳过服务路由
  void SetSkipRouteFilter(bool skip_route_filter);

  /// @brief 设置源服务信息，用于服务路由计算。可选
  ///
  /// @param source_service 源服务信息，包括源服务命名空间和用于过滤的metadata
  void SetSourceService(const ServiceInfo& source_service);

  /// @brief 设置调用哪个set下的服务
  ///
  /// @param set_name 主调指定的被调服务的set名
  bool SetSourceSetName(const std::string& set_name);

  /// @brief 设置调用哪个金丝雀服务实例
  ///
  /// @param canary 主调指定的金丝雀
  void SetCanary(const std::string& canary);

  /// @brief 设置元数据，用于元数据路由
  ///
  /// @param metadata 元数据
  void SetMetadata(std::map<std::string, std::string>& metadata);

  /// @brief 设置元数据路由匹配失败时的降级策略，默认不降级
  ///
  /// @param metadata_failover_type 元数据路由降级策略
  void SetMetadataFailover(MetadataFailoverType metadata_failover_type);

  /// @brief 设置请求流水号。可选，默认随机流水号
  ///
  /// @param flow_id 用于跟踪请求的流水号
  void SetFlowId(uint64_t flow_id);

  /// @brief 设置请求超时时间。可选，默认为全局配置的API超时时间
  ///
  /// @param timeout 设置请求超时时间，可选，单位ms
  void SetTimeout(uint64_t timeout);

  class Impl;
  Impl& GetImpl() const;

 private:
  Impl* impl_;
};

/// @brief 服务实例调用结果上报
class ServiceCallResult : Noncopyable {
 public:
  /// @brief 构造服务实例调用结果上报对象
  ServiceCallResult();

  /// @brief 析构服务实例调用结果上报对象
  ~ServiceCallResult();

  /// @brief 设置服务实例的服务名
  ///
  /// @param service_name 服务名
  void SetServiceName(const std::string& service_name);

  /// @brief 设置服务实例的命名空间
  ///
  /// @param service_namespace 命名空间
  void SetServiceNamespace(const std::string& service_namespace);

  /// @brief 设置服务实例ID
  ///
  /// @param instance_id 服务实例ID
  void SetInstanceId(const std::string& instance_id);

  /// @brief 设置服务实例Host和Port，可选，如果设置了服务实例ID，则这个可不设置，优先使用服务实例ID
  ///
  /// @param host 服务实例Host
  /// @param port 服务实例Port
  void SetInstanceHostAndPort(const std::string& host, int port);

  /// @brief 设置调用返回状态码
  ///
  /// @param ret_status 调用返回状态码
  void SetRetStatus(CallRetStatus ret_status);

  /// @brief 设置调用返回码。可选，用于支持根据返回码实现自己的插件
  ///
  /// @param ret_code
  void SetRetCode(int ret_code);

  /// @brief 设置服务实例调用时延
  ///
  /// @param delay 调用时延
  void SetDelay(uint64_t delay);

  /// @brief 设置主调服务ServiceKey
  ///
  /// @param ServiceKey 主调服务ServiceKey
  void SetSource(const ServiceKey& source);

  /// @brief 设置被调服务subset信息
  ///
  /// @param subset subset信息
  void SetSubset(const std::map<std::string, std::string>& subset);

  /// @brief 设置被调服务labels信息
  ///
  /// @param labels labels信息
  void SetLabels(const std::map<std::string, std::string>& labels);

  /// @brief 设置需要传递的LocalityAware的信息
  ///
  /// @param locality_aware_info LocalityAware的信息
  void SetLocalityAwareInfo(uint64_t locality_aware_info);

  class Impl;
  Impl& GetImpl() const;

 private:
  Impl* impl_;
};

/// @brief 服务实例查询应答
class InstancesResponse : Noncopyable {
 public:
  /// @brief 构造服务实例查询应答对象
  InstancesResponse();

  /// @brief 析构服务实例查询应答对象
  ~InstancesResponse();

  /// @brief 获取应答对应请求流水号
  ///
  /// @return uint64_t 请求流水号
  uint64_t GetFlowId();

  /// @brief 获取服务名
  ///
  /// @return std::string& 服务名
  std::string& GetServiceName();

  /// @brief 获取命名空间
  ///
  /// @return std::string& 命名空间
  std::string& GetServiceNamespace();

  /// @brief 获取服务元数据
  ///
  /// @return std::map<std::string, std::string>&  服务元数据
  std::map<std::string, std::string>& GetMetadata();

  /// @brief 获取权重类型
  ///
  /// @return WeightType 权重类型
  WeightType GetWeightType();

  /// @brief 获取服务版本信息
  ///
  /// @return std::string& 版本信息
  std::string& GetRevision();

  /// @brief 获取服务实例列表
  ///
  /// @return std::vector<Instance>& 服务实例列表
  std::vector<Instance>& GetInstances();

  /// @brief 获取subset信息
  ///
  /// @return const std::map<std::string, std::string>& 实例所属的subset
  const std::map<std::string, std::string>& GetSubset();

  class Impl;
  Impl& GetImpl() const;

 private:
  Impl* impl_;
};

/// @brief 服务数据就绪通知对象接口
class ServiceCacheNotify {
 public:
  virtual ~ServiceCacheNotify() {}

  /// @brief 等待的服务数据准备就绪时执行
  virtual void NotifyReady() = 0;

  /// @brief 等待的服务数据超时时执行
  virtual void NotifyTimeout() = 0;
};

/// @brief 异步获取服务实例对象
class InstancesFuture : Noncopyable {
 public:
  /// @brief 查询异步获取服务实例是否完成
  bool IsDone(bool use_disk_data = true);

  /// @brief 获取服务实例结果，假如异步调用未完成，则阻塞
  ///
  /// @param wait_time 等待时间，单位为毫秒，为0则立即返回
  /// @param result 异步操作返回的实例信息
  /// @return ReturnCode 操作结果
  ///         kReturnOk 表示获取结果成功，结果在result中
  ///         kReturnTimeout 表示获取结果超时，不设置result
  ReturnCode Get(uint64_t wait_time, InstancesResponse*& result);

  /// @brief 设置服务就绪回调，当需要的服务拉取到缓存时执行该回调
  void SetServiceCacheNotify(ServiceCacheNotify* service_cache_notify);

  class Impl;

  /// @brief 构造异步获取服务实例对象
  explicit InstancesFuture(InstancesFuture::Impl* impl);

  /// @brief 析构异步获取服务实例对象
  ~InstancesFuture();

  Impl& GetImpl() const;

 private:
  Impl* impl_;
};

class ConsumerApiImpl;

/// @brief 服务消费端API主接口
class ConsumerApi : Noncopyable {
 public:
  ~ConsumerApi();

  /// @brief 用于提前初始化服务数据
  ///
  /// @param req 获取单个服务实例请求
  /// @return ReturnCode 调用结果
  ReturnCode InitService(const GetOneInstanceRequest& req);

  /// @brief 同步获取单个服务实例
  ///
  /// @param req 获取单个服务实例请求
  /// @param instance 服务实例
  /// @return ReturnCode 调用结果
  ReturnCode GetOneInstance(const GetOneInstanceRequest& req, Instance& instance);

  /// @brief 同步获取单个服务实例
  ///
  /// @param req 获取单个服务实例请求
  /// @param resp 服务实例获取结果，其中只包含一个服务实例
  /// @return ReturnCode 调用结果
  ReturnCode GetOneInstance(const GetOneInstanceRequest& req, InstancesResponse*& resp);

  /// @brief 同步获取批量服务实例
  ///
  /// @note 该接口不会返回熔断半开实例，实例熔断后，进入半开如何没有请求一段时间后会自动恢复
  /// @param req 批量获取服务实例请求
  /// @param resp 服务实例获取结果
  /// @return ReturnCode 调用结果
  ReturnCode GetInstances(const GetInstancesRequest& req, InstancesResponse*& resp);

  /// @brief 同步获取服务下全部服务实例，返回的实例与控制台看到的一致
  ///
  /// @param req 批量获取服务实例请求
  /// @param resp 服务实例获取结果
  /// @return ReturnCode 调用结果
  ReturnCode GetAllInstances(const GetInstancesRequest& req, InstancesResponse*& resp);

  /// @brief 异步获取单个服务路由
  ///
  /// @param req 获取单个服务实例请求
  /// @param future 异步操作future对象，可用于查询和获取操作结果
  /// @return ReturnCode 调用结果
  ReturnCode AsyncGetOneInstance(const GetOneInstanceRequest& req, InstancesFuture*& future);

  /// @brief 异步获取服务路由表
  ///
  /// @param req 批量获取服务实例请求
  /// @param future 异步操作future对象，可用于查询和获取操作结果
  /// @return ReturnCode 调用结果
  ReturnCode AsyncGetInstances(const GetInstancesRequest& req, InstancesFuture*& future);

  /// @brief 上报服务调用结果，用于服务实例熔断和监控统计
  /// @note 本调用没有网络操作，只是将数据写入内存
  ///
  /// @param req 服务实例调用结果
  /// @return ReturnCode 调用结果
  ReturnCode UpdateServiceCallResult(const ServiceCallResult& req);

  /// @brief 拉取路由规则配置的所有key
  ///
  /// @param service_key  需要预拉取规则的服务
  /// @param timeout      预拉取请求超时时间，单位为ms
  /// @param keys         规则里配置的所有key
  /// @return ReturnCode  拉取结果，拉取失败可重试
  ReturnCode GetRouteRuleKeys(const ServiceKey& service_key, uint64_t timeout, const std::set<std::string>*& keys);

  /// @brief 获取对应服务的路由规则(tRPC)
  ///
  /// @param service_key 需要获取路由规则的服务
  /// @param timeout     获取路由规则的超时时间ms
  /// @param json_string Json格式的路由规则
  /// @return ReturnCode 调用结果
  ReturnCode GetServiceRouteRule(const ServiceKey& service_key, uint64_t timeout, std::string& json_string);

  /// @brief 通过Context创建Consumer API对象
  ///
  /// @param Context SDK上下文对象
  /// @return ConsumerApi* 创建失败返回NULL
  static ConsumerApi* Create(Context* context);

  /// @brief 通过配置创建Consumer API对象
  ///
  /// @param config 配置对象
  /// @return Consumer* 创建失败则返回NULL
  static ConsumerApi* CreateFromConfig(Config* config);

  /// @brief 通过配置文件创建Consumer API对象
  ///
  /// @param file 配置文件
  /// @return Consumer* 创建失败返回NULL
  static ConsumerApi* CreateFromFile(const std::string& file);

  /// @brief 通过配置字符串创建Consumer API对象
  ///
  /// @param content 配置字符串
  /// @return Consumer* 创建失败返回NULL
  static ConsumerApi* CreateFromString(const std::string& content);

  /// @brief 从默认文件创建配置对象，默认文件为./polaris.yaml，文件不存在则使用默认配置
  ///
  /// @return ConsumerApi* 创建失败返回NULL
  static ConsumerApi* CreateWithDefaultFile();

 private:
  explicit ConsumerApi(ConsumerApiImpl* impl);
  ConsumerApiImpl* impl_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_CONSUMER_H_
