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
#include <memory>
#include <string>

#include "cache/cache_manager.h"
#include "model/constants.h"
#include "polaris/consumer.h"
#include "polaris/defs.h"
#include "utils/optional.h"

namespace polaris {

class GetOneInstanceRequest::Impl {
 public:
  explicit Impl(const ServiceKey& service_key)
      : service_key_(service_key), load_balance_type_(kLoadBalanceTypeDefaultConfig), backup_instance_num_(0) {}

  ServiceKey service_key_;
  Criteria criteria_;
  std::unique_ptr<ServiceInfo> source_service_;
  optional<uint64_t> flow_id_;  ///< 流水号，用于跟踪用户的请求，可选，默认0
  optional<uint64_t> timeout_;  ///< 本次查询最大超时信息，可选，默认直接获取全局的超时配置
  LoadBalanceType load_balance_type_;
  uint32_t backup_instance_num_;                                 ///< 返回用于重试的实例数量
  std::unique_ptr<std::map<std::string, std::string> > labels_;  ///< 请求标签，用于接口级别熔断
  std::unique_ptr<MetadataRouterParam> metadata_param_;          ///< 请求元数据，用于元数据路由

  ServiceInfo* DumpSourceService() const {
    return source_service_ != nullptr ? new ServiceInfo(*source_service_) : nullptr;
  }

  const std::map<std::string, std::string>& GetLabels() const {
    return labels_ != nullptr ? *labels_ : constants::EmptyStringMap();
  }

  Impl* Dump() const;
};

enum GetInstancesRequstFlag {
  kGetInstancesRequestIncludeCircuitBreaker = 1,
  kGetInstancesRequestIncludeUnhealthy = 1 << 1,
  kGetInstancesRequestSkipRouter = 1 << 2
};

class GetInstancesRequest::Impl {
 public:
  explicit Impl(const ServiceKey& service_key) : service_key_(service_key), request_flag_(0) {}

  ServiceKey service_key_;
  std::unique_ptr<ServiceInfo> source_service_;
  uint8_t request_flag_;
  optional<uint64_t> flow_id_;  ///< 流水号，用于跟踪用户的请求，可选，默认0
  optional<uint64_t> timeout_;  ///< 本次查询最大超时信息，可选，默认直接获取全局的超时配置
  std::unique_ptr<MetadataRouterParam> metadata_param_;  ///< 请求元数据，用于元数据路由

  ServiceInfo* DumpSourceService() const {
    return source_service_ != nullptr ? new ServiceInfo(*source_service_) : nullptr;
  }

  bool GetIncludeCircuitBreakerInstances() const { return request_flag_ & kGetInstancesRequestIncludeCircuitBreaker; }

  bool GetIncludeUnhealthyInstances() const { return request_flag_ & kGetInstancesRequestIncludeUnhealthy; }

  bool GetSkipRouteFilter() const { return request_flag_ & kGetInstancesRequestSkipRouter; }

  Impl* Dump() const;
};

class ServiceCallResult::Impl {
 public:
  Impl() : instance_host_port_(nullptr) {}

  ~Impl() {
    if (instance_host_port_ != nullptr) {
      delete instance_host_port_;
      instance_host_port_ = nullptr;
    }
  }

  InstanceGauge gauge_;

  InstanceHostPortKey* instance_host_port_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_REQUESTS_H_
