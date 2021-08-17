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

/// @file accessors.h
/// @brief 定义接口请求和应答对象的属性存取器，用于插件实现
///
#ifndef POLARIS_CPP_INCLUDE_POLARIS_ACCESSORS_H_
#define POLARIS_CPP_INCLUDE_POLARIS_ACCESSORS_H_

#include <stdint.h>

#include <map>
#include <string>

#include "polaris/defs.h"

namespace polaris {

class GetOneInstanceRequest;
class GetOneInstanceRequestImpl;

class GetOneInstanceRequestAccessor {
public:
  explicit GetOneInstanceRequestAccessor(const GetOneInstanceRequest& request)
      : request_(request) {}

  const ServiceKey& GetServiceKey();

  const Criteria& GetCriteria();

  bool HasSourceService();
  ServiceInfo* GetSourceService();
  ServiceInfo* DumpSourceService();

  bool HasFlowId();
  uint64_t GetFlowId();
  void SetFlowId(uint64_t flow_id);

  bool HasTimeout();
  uint64_t GetTimeout();
  void SetTimeout(uint64_t timeout);

  GetOneInstanceRequest* Dump();

  const std::map<std::string, std::string>& GetLabels() const;

  MetadataRouterParam* GetMetadataParam() const;

  LoadBalanceType GetLoadBalanceType();

private:
  const GetOneInstanceRequest& request_;
};

class GetInstancesRequest;
class GetInstancesRequestImpl;

class GetInstancesRequestAccessor {
public:
  explicit GetInstancesRequestAccessor(const GetInstancesRequest& request) : request_(request) {}

  const ServiceKey& GetServiceKey();

  bool HasSourceService();
  ServiceInfo* GetSourceService();
  ServiceInfo* DumpSourceService();

  bool GetIncludeCircuitBreakerInstances();

  bool GetIncludeUnhealthyInstances();

  bool GetSkipRouteFilter();

  bool HasFlowId();
  uint64_t GetFlowId();
  void SetFlowId(uint64_t flow_id);

  bool HasTimeout();
  uint64_t GetTimeout();
  void SetTimeout(uint64_t timeout);

  MetadataRouterParam* GetMetadataParam() const;

  GetInstancesRequest* Dump();

private:
  const GetInstancesRequest& request_;
};

class ServiceCallResult;
class ServiceCallResultImpl;

class ServiceCallResultGetter {
public:
  explicit ServiceCallResultGetter(const ServiceCallResult& result) : result_(result) {}

  const std::string& GetServiceName();

  const std::string& GetServiceNamespace();

  const std::string& GetInstanceId();

  const std::string& GetHost();

  int GetPort();

  CallRetStatus GetRetStatus();

  const ServiceKey& GetSource();

  const std::map<std::string, std::string>& GetSubset();

  const std::map<std::string, std::string>& GetLabels();

  int GetRetCode();

  uint64_t GetDelay();

private:
  const ServiceCallResult& result_;
};

class Instance;
class InstanceLocalValue;
class InstanceSetter {
public:
  explicit InstanceSetter(Instance& instance) : instance_(instance) {}

  void SetVpcId(const std::string& vpc_id);

  void SetProtocol(const std::string& protocol);

  void SetVersion(const std::string& version);

  void SetPriority(int priority);

  void SetHealthy(bool healthy);

  void SetIsolate(bool isolate);

  void AddMetadataItem(const std::string& key, const std::string& value);

  void SetLogicSet(const std::string& logic_set);

  void SetDynamicWeight(uint32_t dynamic_weight);

  void SetRegion(const std::string& region);

  void SetZone(const std::string& zone);

  void SetCampus(const std::string& campus);

  void SetHashValue(uint64_t hashVal);

  void SetLocalId(uint64_t local_id);

  void SetLocalValue(InstanceLocalValue* localValue);

  void CopyLocalValue(const InstanceSetter& setter);

private:
  Instance& instance_;
};

class InstancesResponse;
class InstancesResponseImpl;
class InstancesResponseSetter {
public:
  explicit InstancesResponseSetter(InstancesResponse& response) : response_(response) {}

  void SetFlowId(const uint64_t flow_id);

  void SetServiceName(const std::string& service_name);

  void SetServiceNamespace(const std::string& service_namespace);

  void SetMetadata(const std::map<std::string, std::string>& metadata);

  void SetWeightType(WeightType weight_type);

  void SetRevision(const std::string& revision);

  void AddInstance(const Instance& instance);

  void SetSubset(const std::map<std::string, std::string>& subset);

private:
  InstancesResponse& response_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_ACCESSORS_H_
