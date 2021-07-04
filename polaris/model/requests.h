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

#ifndef POLARIS_CPP_POLARIS_MODEL_REQUESTS_H_
#define POLARIS_CPP_POLARIS_MODEL_REQUESTS_H_

#include <stdint.h>

#include <map>
#include <string>

#include "polaris/defs.h"
#include "utils/optional.h"
#include "utils/scoped_ptr.h"

namespace polaris {

const std::map<std::string, std::string>& EmptyStringMap();

class GetOneInstanceRequestImpl {
public:
  GetOneInstanceRequestImpl() : load_balance_type_(kLoadBalanceTypeDefaultConfig) {}

  ServiceKey service_key_;
  Criteria criteria_;
  ScopedPtr<ServiceInfo> source_service_;
  optional<uint64_t> flow_id_;  ///< 流水号，用于跟踪用户的请求，可选，默认0
  optional<uint64_t> timeout_;  ///< 本次查询最大超时信息，可选，默认直接获取全局的超时配置
  LoadBalanceType load_balance_type_;
  ScopedPtr<std::map<std::string, std::string> > labels_;  ///< 请求标签，用于接口级别熔断
  ScopedPtr<MetadataRouterParam> metadata_param_;  ///< 请求元数据，用于元数据路由
};

enum GetInstancesRequstFlag {
  kGetInstancesRequestIncludeCircuitBreaker = 1,
  kGetInstancesRequestIncludeUnhealthy      = 1 << 1,
  kGetInstancesRequestSkipRouter            = 1 << 2
};

class GetInstancesRequestImpl {
public:
  GetInstancesRequestImpl() : request_flag_(0) {}

  ServiceKey service_key_;
  ScopedPtr<ServiceInfo> source_service_;
  uint8_t request_flag_;
  optional<uint64_t> flow_id_;  ///< 流水号，用于跟踪用户的请求，可选，默认0
  optional<uint64_t> timeout_;  ///< 本次查询最大超时信息，可选，默认直接获取全局的超时配置
  ScopedPtr<MetadataRouterParam> metadata_param_;  ///< 请求元数据，用于元数据路由
};

class ServiceCallResultImpl {
public:
  ServiceCallResultImpl() : port_(0), ret_status_(kCallRetOk), ret_code_(0), delay_(0) {}

public:
  std::string service_namespace_;
  std::string service_name_;
  std::string instance_id_;
  std::string host_;
  int port_;

  CallRetStatus ret_status_;
  int ret_code_;
  uint64_t delay_;

  ServiceKey source_;
  std::map<std::string, std::string> subset_;
  std::map<std::string, std::string> labels_;
};

/// @brief 服务注册请求实现类
class InstanceRegisterRequestImpl {
public:
  std::string service_namespace_;  ///< 名字空间
  std::string service_name_;       ///< 服务名
  std::string service_token_;      ///< 服务访问Token
  std::string host_;               ///< 服务监听host，支持IPv6地址
  int port_;                       ///< 服务实例监听port

  optional<uint64_t> flow_id_;  ///< 流水号，用于跟踪用户的请求，可选，默认0
  optional<uint64_t> timeout_;  ///< 本次查询最大超时信息，可选，默认直接获取全局的超时配置
  optional<std::string> vpc_id_;    ///< 服务实例vpc id，可选
  optional<std::string> protocol_;  ///< 服务协议，可选
  optional<int> weight_;            ///< 服务权重，默认100，范围0-1000
  optional<int> priority_;  ///< 实例优先级，默认为0，数值越小，优先级越高
  optional<std::string> version_;                           ///< 实例提供服务版本号
  optional<std::map<std::string, std::string> > metadata_;  ///< 用户自定义metadata信息
  optional<bool> health_check_flag_;             ///< 是否开启健康检查，默认不开启
  optional<HealthCheckType> health_check_type_;  ///< 健康检查类型
  optional<int> ttl_;                            ///< ttl超时时间，单位：秒
};

class InstanceDeregisterRequestImpl {
public:
  std::string service_namespace_;  ///< 名字空间
  std::string service_name_;
  std::string service_token_;          ///< 服务访问Token
  optional<std::string> vpc_id_;       ///< 服务实例vpc id，可选
  std::string host_;                   ///< 服务实例Host
  int port_;                           ///< 服务实例Port
  optional<std::string> instance_id_;  ///< 服务实例ID
  optional<uint64_t> flow_id_;  ///< 流水号，用于跟踪用户的请求，可选，默认0
  optional<uint64_t> timeout_;  ///< 本次查询最大超时信息，可选，默认直接获取全局的超时配置
};

class InstanceHeartbeatRequestImpl {
public:
  std::string service_namespace_;  ///< 名字空间
  std::string service_name_;
  std::string service_token_;          ///< 服务访问Token
  optional<std::string> vpc_id_;       ///< 服务实例vpc id，可选
  std::string host_;                   ///< 服务实例Host
  int port_;                           ///< 服务实例Port
  optional<std::string> instance_id_;  ///< 服务实例ID
  optional<uint64_t> flow_id_;  ///< 流水号，用于跟踪用户的请求，可选，默认0
  optional<uint64_t> timeout_;  ///< 本次查询最大超时信息，可选，默认直接获取全局的超时配置
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_REQUESTS_H_
