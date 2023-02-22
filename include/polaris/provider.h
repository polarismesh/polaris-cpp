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

#ifndef POLARIS_CPP_INCLUDE_POLARIS_PROVIDER_H_
#define POLARIS_CPP_INCLUDE_POLARIS_PROVIDER_H_

#include <map>
#include <string>

#include "polaris/defs.h"
#include "polaris/noncopyable.h"

namespace polaris {

/// @brief 服务实例注册请求
///
/// 用于向指定命令空间的服务注册服务实例。必须拥有服务token才能进行服务实例注册。
/// 服务实例注册成功后，其他服务调用服务发现接口能发现该服务实例，可能会立即向该服务实例发送请求。
/// @note 所以必须在服务实例启动完成后才去进行服务注册。
class InstanceRegisterRequest : Noncopyable {
 public:
  /// @brief 构造服务实例注册请求对象
  ///
  /// @param service_namespace 服务名所属命名空间
  /// @param service_name 服务名
  /// @param service_token 服务名对应的token
  /// @param host 服务实例监听地址
  /// @param port 服务实例监听端口
  InstanceRegisterRequest(const std::string& service_namespace, const std::string& service_name,
                          const std::string& service_token, const std::string& host, int port);

  /// @brief 析构服务实例注册请求对象
  ~InstanceRegisterRequest();

  /// @brief 设置请求超时时间。可选，默认为SDK配置的API超时时间
  ///
  /// @param timeout 设置请求超时时间，可选，单位ms
  void SetTimeout(uint64_t timeout);

  /// @brief 设置服务实例的VPC ID。可选，默认为空
  ///
  /// @param vpc_id 服务实例的host:port所在vpc id
  void SetVpcId(const std::string& vpc_id);

  /// @brief 设置服务实例协议。可选，默认为空
  ///
  /// @param protocol 服务实例协议
  void SetProtocol(const std::string& protocol);

  /// @brief 设置服务实例权重。可选，默认为100
  ///
  /// @param weight 服务实例权重
  void SetWeight(int weight);

  /// @brief 设置服务实例优先级。可选，置默认为0
  ///
  /// @param priority 服务实例优先级
  void SetPriority(int priority);

  /// @brief 设置服务实例版本信息。可选，默认为空
  ///
  /// @param version 服务实例版本
  void SetVersion(const std::string& version);

  /// @brief 设置服务实例的metada数据。可选，默认为空
  ///
  /// @param metadata 服务实例metadata
  void SetMetadata(const std::map<std::string, std::string>& metadata);

  /// @brief 设置服务实例是否开启健康检查。可选，默认不开启
  ///
  /// @param health_check_flag
  void SetHealthCheckFlag(bool health_check_flag);

  /// @brief 设置健康检查类型。可选，默认为心跳健康检查
  ///
  /// @param health_check_type 健康检查类型
  void SetHealthCheckType(HealthCheckType health_check_type);

  /// @brief 设置心跳健康检查ttl，单位为s，不填默认为5s，
  ///
  /// 开启了心跳健康检查，客户端必须以TTL间隔上报心跳
  /// 健康检查服务器3个TTL未受到心跳则将实例置为不健康
  /// @param ttl 心跳检查ttl
  void SetTtl(int ttl);

  /// @brief 设置请求流水号。可选，默认随机流水号
  ///
  /// @param flow_id 用于跟踪请求的流水号
  void SetFlowId(uint64_t flow_id);

  /// @brief 设置节点的位置信息。可选，默认会从公司CMDB获取
  ///
  /// @param region 节点所在区域
  /// @param zone 节点所在城市
  /// @param campus 节点所在园区
  void SetLocation(const std::string& region, const std::string& zone, const std::string& campus);

  /// @brief 设置实例id，可选，默认会在服务端生产实例id
  ///
  /// @param instance_id 提供的实例id
  void SetInstanceId(const std::string& instance_id);

  class Impl;
  Impl& GetImpl() const;

 private:
  Impl* impl_;
};

/// @brief 服务实例反注册请求
class InstanceDeregisterRequest : Noncopyable {
 public:
  /// @brief 构造服务实例反注册请求对象，使用服务实例ID表示服务实例
  ///
  /// @param service_token 服务token
  /// @param instance_id 服务实例ID
  InstanceDeregisterRequest(const std::string& service_token, const std::string& instance_id);

  /// @brief
  /// 构造服务实例反注册请求对象，使用服务实例四元组（命名空间，服务名，host，port）表示服务实例
  ///
  /// @param service_namespace 命名空间
  /// @param service_name 服务名
  /// @param service_token 服务token
  /// @param host 服务实例host
  /// @param port 服务实例port
  InstanceDeregisterRequest(const std::string& service_namespace, const std::string& service_name,
                            const std::string& service_token, const std::string& host, int port);

  /// @brief 析构服务实例反注册请求对象
  ~InstanceDeregisterRequest();

  /// @brief 设置请求超时时间。可选，默认为全局配置的API超时时间
  ///
  /// @param timeout 设置请求超时时间，可选，单位ms
  void SetTimeout(uint64_t timeout);

  /// @brief 设置服务实例的VPC ID
  ///
  /// @param vpc_id 服务实例的host:port所在vpc id
  void SetVpcId(const std::string& vpc_id);

  /// @brief 设置请求流水号。可选，默认随机流水号
  ///
  /// @param flow_id 用于跟踪请求的流水号
  void SetFlowId(uint64_t flow_id);

  class Impl;
  Impl& GetImpl() const;

 private:
  Impl* impl_;
};

/// @brief 服务实例心跳上报请求
class InstanceHeartbeatRequest : Noncopyable {
 public:
  /// @brief 构造服务实例心跳上报请求对象，使用服务实例ID表示服务实例
  ///
  /// @param service_token 服务token
  /// @param instance_id 服务实例ID
  InstanceHeartbeatRequest(const std::string& service_token, const std::string& instance_id);

  /// @brief
  /// 构造服务实例心跳上报请求对象，使用服务实例四元组（命名空间，服务名，host，port）表示服务实例
  ///
  /// @param service_namespace 命名空间
  /// @param service_name 服务名
  /// @param service_token 服务token
  /// @param host 服务实例host
  /// @param port 服务实例port
  InstanceHeartbeatRequest(const std::string& service_namespace, const std::string& service_name,
                           const std::string& service_token, const std::string& host, int port);

  /// @brief 析构服务实例心跳上报请求对象
  ~InstanceHeartbeatRequest();

  /// @brief 设置服务实例的VPC ID
  ///
  /// @param vpc_id 服务实例的host:port所在vpc id
  void SetVpcId(const std::string& vpc_id);

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

/// @brief Provider异步接口回调函数
class ProviderCallback {
 public:
  virtual ~ProviderCallback() {}

  /// @brief 应答回调
  ///
  /// @param code     返回码
  /// @param message  返回消息
  virtual void Response(ReturnCode code, const std::string& message) = 0;
};

// forward declaration
class Context;
class Config;

/// @brief Provider API 被调服务实例用于注册、反注册、心跳上报
///
/// 服务启动后先进行注册，注册成功会返回服务实例ID。然后可使用该服务实例ID进行反注册和心跳上报
/// @note 服务端接口必须在请求中传入服务token。服务token可在polaris 控制台上查看
/// @note 该接口线程安全，整个进程创建一个即可
class ProviderApi : Noncopyable {
 public:
  ~ProviderApi();

  /// @brief 同步注册服务实例
  ///
  /// 服务注册成功后会填充instance中的instance_id字段，用户可保持该instance对象用于反注册和心跳上报
  ///
  /// @param req 实例注册请求
  /// @param instance_id 注册成功返回的服务实例ID
  /// @return ReturnCode 调用返回码
  ///         kReturnOk 服务注册成功，instance_id有返回值
  ///         kReturnExistedResource 服务实例已存在，instance_id有返回值
  ///         其他返回码表示服务注册失败
  ReturnCode Register(const InstanceRegisterRequest& req, std::string& instance_id);

  /// @brief 同步反注册服务实例
  ///
  /// @param req 服务实例反注册请求
  /// @return ReturnCode 调用返回码
  ///         kReturnOk 表示服务反注册成功
  ///         其他返回码表示服务反注册失败
  ReturnCode Deregister(const InstanceDeregisterRequest& req);

  /// @brief 服务实例心跳上报
  ///
  /// @param req 服务实例心跳上报请求
  /// @return ReturnCode 调用返回码
  ///         kReturnOk 表示心跳上报成功
  ///         kRetrunRateLimit 表示服务心跳上报频率过快，程序应降低心跳上报频率
  ///         kReturnServiceNotFound 如果刚刚注册即发起心跳上报可能返回服务不存在，重试即可
  ///         其他返回码表示心跳上报失败，可重试
  ReturnCode Heartbeat(const InstanceHeartbeatRequest& req);

  /// @brief 异步服务实例心跳上报
  ///
  /// @param req 服务实例心跳上报请求
  /// @param callback 请求回调
  /// @return ReturnCode 调用返回码
  ///         kReturnOk 表示上报请求成功插入请求队列
  ReturnCode AsyncHeartbeat(const InstanceHeartbeatRequest& req, ProviderCallback* callback);

  /// @brief 通过Context创建Provider API对象
  ///
  /// @param Context SDK上下文对象
  /// @return ProviderApi* 创建失败返回NULL
  static ProviderApi* Create(Context* Context);

  /// @brief 通过配置创建Provider API对象
  ///
  /// @param config 配置对象
  /// @return ProviderApi* 创建失败则返回NULL
  static ProviderApi* CreateFromConfig(Config* config);

  /// @brief 通过配置文件创建Provider API对象
  ///
  /// @param file 配置文件
  /// @return ProviderApi* 创建失败返回NULL
  static ProviderApi* CreateFromFile(const std::string& file);

  /// @brief 通过配置字符串创建Provider API对象
  ///
  /// @param content 配置字符串
  /// @return ProviderApi* 创建失败返回NULL
  static ProviderApi* CreateFromString(const std::string& content);

  /// @brief 从默认文件创建配置对象，默认文件为./polaris.yaml，文件不存在则使用默认配置
  ///
  /// @return ProviderApi* 创建失败返回NULL
  static ProviderApi* CreateWithDefaultFile();

  class Impl;
  Impl& GetImpl() const;

private:
  explicit ProviderApi(Impl* impl);

  Impl* impl_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_PROVIDER_H_
