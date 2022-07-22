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

#ifndef POLARIS_CPP_POLARIS_PROVIDER_REQUEST_H_
#define POLARIS_CPP_POLARIS_PROVIDER_REQUEST_H_

#include "polaris/provider.h"

#include "polaris/defs.h"
#include "utils/optional.h"
#include "v1/request.pb.h"

namespace polaris {

class ProviderRequestBase {
 public:
  ProviderRequestBase() : port_(0), flow_id_(0) {}

  void SetWithHostPort(const std::string& service_namespace, const std::string& service_name,
                       const std::string& service_token, const std::string& host, int port) {
    service_namespace_ = service_namespace;
    service_name_ = service_name;
    service_token_ = service_token;
    host_ = host;
    port_ = port;
  }

  // 检查参数是否合法
  bool CheckRequest(const char* request_type) const;

  void SetVpcId(const std::string& vpc_id) { vpc_id_ = vpc_id; }

  void SetTimeout(uint64_t timeout) { timeout_ = timeout; }
  bool HasTimeout() const { return timeout_.HasValue(); }
  uint64_t GetTimeout() const { return timeout_.Value(); }

  void SetFlowId(uint64_t flow_id) { flow_id_ = flow_id; }
  uint64_t GetFlowId() const { return flow_id_; }
  const std::string GetNamespace() const { return service_namespace_; }
  const std::string GetService() const { return service_name_; }
  const std::string GetToken() const { return service_token_; }
  const std::string GetHost() const { return host_; }
  int GetPort() const { return port_; }
  const std::string GetVpcId() const { return vpc_id_; }

 protected:
  std::string service_namespace_;  ///< 服务名字空间
  std::string service_name_;       ///< 服务名字
  std::string service_token_;      ///< 服务访问token
  std::string host_;               ///< 服务实例host，支持IPv6地址
  int port_;                       ///< 服务实例port

  std::string vpc_id_;  ///< 服务实例vpc id，可选

  optional<uint64_t> timeout_;  ///< 本次查询最大超时信息，可选，默认使用SDK API超时配置
  uint64_t flow_id_;
};

/// @brief 服务注册请求实现类
class InstanceRegisterRequest::Impl : public ProviderRequestBase {
 public:
  Impl() : health_check_flag_(false) {}

  void AddMetdata(const std::string& key, const std::string& value);

  v1::Instance* ToPb() const;  // 转换成PB请求

 private:
  friend class InstanceRegisterRequest;

  std::string protocol_;                         ///< 服务协议，可选
  optional<int> weight_;                         ///< 服务权重，默认100，范围0-1000
  optional<int> priority_;                       ///< 实例优先级，默认为0，数值越小，优先级越高
  std::string version_;                          ///< 实例提供服务版本号
  std::map<std::string, std::string> metadata_;  ///< 用户自定义metadata信息
  bool health_check_flag_;                       ///< 是否开启健康检查，默认不开启
  optional<HealthCheckType> health_check_type_;  ///< 健康检查类型
  optional<int> ttl_;                            ///< ttl超时时间，单位：秒

  // 位置信息
  std::string region_;
  std::string zone_;
  std::string campus_;
};

// 唯一标识实例的请求，用于反注册和心跳上报
class InstanceIdentityRequest : public ProviderRequestBase {
 public:
  void SetWithId(const std::string& service_token, const std::string& instance_id) {
    service_token_ = service_token;
    instance_id_ = instance_id;
  }

  bool CheckRequest(const char* request_type) const;

  v1::Instance* ToPb() const;  // 转换成PB请求
  const std::string GetInstanceID() const { return instance_id_.Value(); }

 private:
  optional<std::string> instance_id_;  // 服务实例ID
};

// 服务实例反注册
class InstanceDeregisterRequest::Impl : public InstanceIdentityRequest {};

// 服务实例心跳上报
class InstanceHeartbeatRequest::Impl : public InstanceIdentityRequest {};

}  // namespace polaris

#endif  // POLARIS_CPP_POLARIS_PROVIDER_REQUEST_H_
