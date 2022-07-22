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

#ifndef POLARIS_CPP_POLARIS_API_CONSUMER_API_H_
#define POLARIS_CPP_POLARIS_API_CONSUMER_API_H_

#include <stdint.h>
#include <string>

#include "context/context_impl.h"
#include "context/service_context.h"
#include "model/requests.h"
#include "model/responses.h"
#include "model/return_code.h"
#include "monitor/api_stat.h"
#include "polaris/consumer.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "utils/ref_count.h"

namespace polaris {

class InstancesFuture::Impl : public AtomicRefCount {
 public:
  Impl(const ServiceKey& service_key, ApiStat* api_stat, ContextImpl* context_impl, ServiceInfo* source_service_info);

  static InstancesFuture* CreateInstancesFuture(ApiStat* api_stat, ContextImpl* context_impl,
                                                ServiceContext* service_context, GetOneInstanceRequest::Impl& req_impl);

  static InstancesFuture* CreateInstancesFuture(ApiStat* api_stat, ContextImpl* context_impl,
                                                ServiceContext* service_context, GetInstancesRequest::Impl& req_impl);

  ReturnCode CheckReady();

 private:
  virtual ~Impl();

 private:
  friend class InstancesFuture;
  friend class CacheManager;
  friend class TimeoutWatcher;
  ApiStat* api_stat_;
  ContextImpl* const context_impl_;
  GetOneInstanceRequest::Impl* one_instance_req_;
  GetInstancesRequest::Impl* instances_req_;
  uint64_t request_timeout_;
  // 保存ServiceInfo结构数据，RouteInfo内部不维护
  ServiceInfo* source_service_info_;
  RouteInfo route_info_;
  RouteInfoNotify* route_info_notify_;
};

// POLARIS 客户端API的主接口
class ConsumerApiImpl {
 public:
  explicit ConsumerApiImpl(Context* context);
  ~ConsumerApiImpl();

  static ReturnCode PrepareRouteInfo(ServiceContext* service_context, RouteInfo& route_info, const char* action,
                                     uint64_t request_timeout);

  static ReturnCode GetOneInstance(ServiceContext* service_context, RouteInfo& route_info,
                                   GetOneInstanceRequest::Impl& req_impl, Instance& instance);

  static ReturnCode GetOneInstance(ServiceContext* service_context, RouteInfo& route_info,
                                   GetOneInstanceRequest::Impl& req_impl, InstancesResponse*& resp);

  static ReturnCode GetInstances(ServiceContext* service_context, RouteInfo& route_info,
                                 GetInstancesRequest::Impl& req_impl, InstancesResponse*& resp);

  static ReturnCode UpdateServiceCallResult(Context* context, const InstanceGauge& gauge);

  static ReturnCode GetSystemServer(Context* context, const ServiceKey& service_key, const Criteria& criteria,
                                    Instance*& instance, uint64_t timeout, const std::string& protocol = "grpc");

  static void UpdateServerResult(Context* context, const ServiceKey& service_key, const Instance& instance,
                                 PolarisServerCode code, CallRetStatus status, uint64_t delay);


  Context* GetContext() const { return context_; }

 private:
  static void GetBackupInstances(ServiceInstances* service_instances, LoadBalancer* load_balancer,
                                 uint32_t backup_instance_num, const Criteria& criteria,
                                 std::vector<Instance*>& backup_instances);

 private:
  Context* const context_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_API_CONSUMER_API_H_
