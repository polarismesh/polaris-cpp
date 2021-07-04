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

#include "polaris/defs.h"
#include "polaris/model.h"
#include "utils/ref_count.h"

namespace polaris {

class ApiStat;
class Context;
class ContextImpl;
class ConsumerApiImpl;
class GetInstancesRequest;
class GetInstancesRequestAccessor;
class GetOneInstanceRequest;
class GetOneInstanceRequestAccessor;
class InstancesFuture;
class InstancesResponse;
class RouteInfoNotify;
class ServiceContext;
struct InstanceGauge;

class InstancesFutureImpl : public AtomicRefCount {
public:
  InstancesFutureImpl(const ServiceKey& service_key, ServiceInfo* source_service_info);

  static InstancesFuture* CreateInstancesFuture(ApiStat* api_stat, ContextImpl* context_impl,
                                                ServiceContext* service_context,
                                                GetOneInstanceRequest* req);

  static InstancesFuture* CreateInstancesFuture(ApiStat* api_stat, ContextImpl* context_impl,
                                                ServiceContext* service_context,
                                                GetInstancesRequest* req);

  ReturnCode CheckReady();

private:
  virtual ~InstancesFutureImpl();

private:
  friend class InstancesFuture;
  friend class CacheManager;
  friend class TimeoutWatcher;
  ApiStat* api_stat_;
  ContextImpl* context_impl_;
  ServiceContext* service_context_;
  GetOneInstanceRequest* one_instance_req_;
  GetInstancesRequest* instances_req_;
  uint64_t request_timeout_;
  RouteInfo route_info_;
  RouteInfoNotify* route_info_notify_;
};

// POLARIS 客户端API的主接口
class ConsumerApiImpl {
public:
  explicit ConsumerApiImpl(Context* context);
  ~ConsumerApiImpl();

  static ReturnCode PrepareRouteInfo(ServiceContext* service_context, RouteInfo& route_info,
                                     const char* action, uint64_t request_timeout);

  static ReturnCode GetOneInstance(ServiceContext* service_context, RouteInfo& route_info,
                                   GetOneInstanceRequestAccessor& request, Instance& instance);

  static ReturnCode GetOneInstance(ServiceContext* service_context, RouteInfo& route_info,
                                   GetOneInstanceRequestAccessor& request,
                                   InstancesResponse*& resp);

  static ReturnCode GetInstances(ServiceContext* service_context, RouteInfo& route_info,
                                 GetInstancesRequestAccessor& request, InstancesResponse*& resp);

  static ReturnCode UpdateServiceCallResult(Context* context, const InstanceGauge& gauge);

  static ReturnCode GetSystemServer(Context* context, const ServiceKey& service_key,
                                    const Criteria& criteria, Instance*& instance, uint64_t timeout,
                                    const std::string& protocol = "grpc");

private:
  friend class ConsumerApi;
  friend class InstancesFuture;
  friend class InstancesFutureImpl;

  Context* context_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_API_CONSUMER_API_H_
