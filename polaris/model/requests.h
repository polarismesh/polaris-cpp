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
  GetOneInstanceRequestImpl()
      : load_balance_type_(kLoadBalanceTypeDefaultConfig), backup_instance_num_(0) {}

  ServiceKey service_key_;
  Criteria criteria_;
  ScopedPtr<ServiceInfo> source_service_;
  optional<uint64_t> flow_id_;  ///< 流水号，用于跟踪用户的请求，可选，默认0
  optional<uint64_t> timeout_;  ///< 本次查询最大超时信息，可选，默认直接获取全局的超时配置
  LoadBalanceType load_balance_type_;
  uint32_t backup_instance_num_;                           ///< 返回用于重试的实例数量
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
  ServiceCallResultImpl()
      : port_(0), ret_status_(kCallRetOk), ret_code_(0), delay_(0), locality_aware_info_(0) {}

public:
  std::string service_namespace_;
  std::string service_name_;
  std::string instance_id_;
  std::string host_;
  int port_;

  CallRetStatus ret_status_;
  int ret_code_;
  uint64_t delay_;
  uint64_t locality_aware_info_;

  ServiceKey source_;
  std::map<std::string, std::string> subset_;
  std::map<std::string, std::string> labels_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_REQUESTS_H_
