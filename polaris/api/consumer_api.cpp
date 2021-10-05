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

#include "api/consumer_api.h"

#include <time.h>

#include <iosfwd>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "cache/cache_manager.h"
#include "context_internal.h"
#include "logger.h"
#include "model/model_impl.h"
#include "monitor/api_stat.h"
#include "plugin/load_balancer/locality_aware/locality_aware.h"
#include "polaris/accessors.h"
#include "polaris/config.h"
#include "polaris/consumer.h"
#include "polaris/context.h"
#include "polaris/defs.h"
#include "polaris/plugin.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

#ifndef ONLY_RATE_LIMIT
ConsumerApi::ConsumerApi(ConsumerApiImpl* impl) { impl_ = impl; }

ConsumerApi::~ConsumerApi() {
  if (impl_ != NULL) {
    delete impl_;
    impl_ = NULL;
  }
}

ConsumerApi* ConsumerApi::Create(Context* context) {
  if (context == NULL) {
    POLARIS_LOG(LOG_ERROR, "create consumer api failed because context is null");
    return NULL;
  }
  if (context->GetContextMode() != kPrivateContext && context->GetContextMode() != kShareContext &&
      context->GetContextMode() != kLimitContext) {
    POLARIS_LOG(LOG_ERROR, "create consumer api failed because context is init with error mode");
    return NULL;
  }
  ConsumerApiImpl* api_impl = new ConsumerApiImpl(context);
  return new ConsumerApi(api_impl);
}

ConsumerApi* ConsumerApi::CreateFromConfig(Config* config) {
  if (config == NULL) {
    POLARIS_LOG(LOG_WARN, "create consumer api failed because parameter config is null");
    return NULL;
  }
  Context* context = Context::Create(config, kPrivateContext);
  if (context == NULL) {
    return NULL;
  }
  return ConsumerApi::Create(context);
}

static ConsumerApi* CreateWithConfig(Config* config, const std::string& err_msg) {
  if (config == NULL) {
    POLARIS_LOG(LOG_ERROR, "init config with error: %s", err_msg.c_str());
    return NULL;
  }
  ConsumerApi* consumer = ConsumerApi::CreateFromConfig(config);
  delete config;
  return consumer;
}

ConsumerApi* ConsumerApi::CreateFromFile(const std::string& file) {
  std::string err_msg;
  return CreateWithConfig(Config::CreateFromFile(file, err_msg), err_msg);
}

ConsumerApi* ConsumerApi::CreateFromString(const std::string& content) {
  std::string err_msg;
  return CreateWithConfig(Config::CreateFromString(content, err_msg), err_msg);
}

ConsumerApi* ConsumerApi::CreateWithDefaultFile() {
  std::string err_msg;
  return CreateWithConfig(Config::CreateWithDefaultFile(err_msg), err_msg);
}

InstancesFuture::InstancesFuture(InstancesFutureImpl* impl) { impl_ = impl; }

InstancesFuture::~InstancesFuture() {
  if (impl_ != NULL) {
    impl_->DecrementRef();
    impl_ = NULL;
  }
}

InstancesFutureImpl::InstancesFutureImpl(const ServiceKey& service_key,
                                         ServiceInfo* source_service_info)
    : route_info_(service_key, source_service_info) {
  context_impl_      = NULL;
  service_context_   = NULL;
  api_stat_          = NULL;
  one_instance_req_  = NULL;
  instances_req_     = NULL;
  route_info_notify_ = NULL;
  request_timeout_   = 0;
}

InstancesFutureImpl::~InstancesFutureImpl() {
  context_impl_ = NULL;
  POLARIS_ASSERT(service_context_ != NULL);
  service_context_->DecrementRef();
  if (api_stat_ != NULL) {
    delete api_stat_;
    api_stat_ = NULL;
  }
  if (one_instance_req_ != NULL) {
    delete one_instance_req_;
    one_instance_req_ = NULL;
  }
  if (instances_req_ != NULL) {
    delete instances_req_;
    instances_req_ = NULL;
  }
  if (route_info_notify_ != NULL) {
    delete route_info_notify_;
    route_info_notify_ = NULL;
  }
}

InstancesFuture* InstancesFutureImpl::CreateInstancesFuture(ApiStat* api_stat,
                                                            ContextImpl* context_impl,
                                                            ServiceContext* service_context,
                                                            GetOneInstanceRequest* req) {
  GetOneInstanceRequestAccessor request(*req);
  InstancesFutureImpl* impl =
      new InstancesFutureImpl(request.GetServiceKey(), request.DumpSourceService());
  impl->context_impl_              = context_impl;
  impl->api_stat_                  = api_stat;
  impl->service_context_           = service_context;
  impl->one_instance_req_          = req;
  impl->request_timeout_           = request.GetTimeout();
  ServiceRouterChain* router_chain = service_context->GetServiceRouterChain();
  impl->route_info_notify_         = router_chain->PrepareRouteInfoWithNotify(impl->route_info_);
  return new InstancesFuture(impl);
}

InstancesFuture* InstancesFutureImpl::CreateInstancesFuture(ApiStat* api_stat,
                                                            ContextImpl* context_impl,
                                                            ServiceContext* service_context,
                                                            GetInstancesRequest* req) {
  GetInstancesRequestAccessor request(*req);
  InstancesFutureImpl* impl =
      new InstancesFutureImpl(request.GetServiceKey(), request.DumpSourceService());
  impl->context_impl_              = context_impl;
  impl->service_context_           = service_context;
  impl->api_stat_                  = api_stat;
  impl->instances_req_             = req;
  impl->request_timeout_           = request.GetTimeout();
  ServiceRouterChain* router_chain = service_context->GetServiceRouterChain();
  impl->route_info_notify_         = router_chain->PrepareRouteInfoWithNotify(impl->route_info_);
  return new InstancesFuture(impl);
}

ReturnCode InstancesFutureImpl::CheckReady() {
  if (route_info_notify_ == NULL || route_info_notify_->IsDataReady(false)) {
    return kReturnOk;
  } else {  // 查询未就绪则等待
    timespec ts = Time::CurrentTimeAddWith(0);
    return route_info_notify_->WaitData(ts);
  }
}

bool InstancesFuture::IsDone(bool use_disk_data) {
  return impl_->route_info_notify_ == NULL || impl_->route_info_notify_->IsDataReady(use_disk_data);
}

ReturnCode InstancesFuture::Get(uint64_t wait_time, InstancesResponse*& result) {
  ReturnCode ret = kReturnOk;
  impl_->context_impl_->RcuEnter();
  if (impl_->route_info_notify_ != NULL) {  // 还有数据未就绪
    bool use_disk_data = false;
    if (!impl_->route_info_notify_->IsDataReady(use_disk_data)) {  // 查询未就绪则等待
      timespec ts = Time::CurrentTimeAddWith(wait_time);
      ret         = impl_->route_info_notify_->WaitData(ts);
    }
    // 查询得到数据就绪，或等待数据就绪成功。或等待失败但存在磁盘数据
    use_disk_data = true;
    if (ret == kReturnOk || impl_->route_info_notify_->IsDataReady(use_disk_data)) {
      ret = impl_->route_info_notify_->SetDataToRouteInfo(impl_->route_info_);
      delete impl_->route_info_notify_;  // 数据准备就绪之后释放notify
      impl_->route_info_notify_ = NULL;
    }
  }
  if (ret == kReturnOk) {
    if (impl_->one_instance_req_ != NULL) {
      GetOneInstanceRequestAccessor request(*impl_->one_instance_req_);
      ret = ConsumerApiImpl::GetOneInstance(impl_->service_context_, impl_->route_info_, request,
                                            result);
    } else {
      GetInstancesRequestAccessor request(*impl_->instances_req_);
      ret = ConsumerApiImpl::GetInstances(impl_->service_context_, impl_->route_info_, request,
                                          result);
    }
  }
  impl_->context_impl_->RcuExit();
  impl_->api_stat_->Record(ret);
  return ret;
}

void InstancesFuture::SetServiceCacheNotify(ServiceCacheNotify* service_cache_notify) {
  impl_->context_impl_->RcuEnter();
  ReturnCode ret = impl_->CheckReady();
  impl_->context_impl_->RcuExit();
  if (ret == kReturnOk) {  // 如果数据已经就绪则直接触发回调并返回
    service_cache_notify->NotifyReady();
    delete service_cache_notify;
    return;
  }
  CacheManager* cache_manager = impl_->context_impl_->GetCacheManager();
  cache_manager->RegisterTimeoutWatcher(impl_, service_cache_notify);
}

ConsumerApiImpl::ConsumerApiImpl(Context* context) {
  context_ = context;
  srand(time(NULL));
}

ConsumerApiImpl::~ConsumerApiImpl() {
  if (context_ != NULL && context_->GetContextMode() == kPrivateContext) {
    delete context_;
  }
}

ReturnCode ConsumerApiImpl::PrepareRouteInfo(ServiceContext* service_context, RouteInfo& route_info,
                                             const char* action, uint64_t request_timeout) {
  // 准备路由过滤数据
  ServiceRouterChain* router_chain = service_context->GetServiceRouterChain();
  ReturnCode ret_code              = router_chain->PrepareRouteInfo(route_info, request_timeout);
  if (ret_code != kReturnOk) {
    const ServiceKey& service_key = route_info.GetServiceKey();
    POLARIS_LOG(LOG_ERROR, "%s prepare route info for service[%s/%s] with error:%s", action,
                service_key.namespace_.c_str(), service_key.name_.c_str(),
                ReturnCodeToMsg(ret_code).c_str());
    return ret_code;
  }
  // 触发（非阻塞）拉取熔断配置
  service_context->GetCircuitBreakerChain()->PrepareServicePbConfTrigger();
  return ret_code;
}

ReturnCode ConsumerApiImpl::GetOneInstance(ServiceContext* service_context, RouteInfo& route_info,
                                           GetOneInstanceRequestAccessor& request,
                                           Instance& instance) {
  const ServiceKey& service_key = route_info.GetServiceKey();
  if (!request.GetLabels().empty()) {
    route_info.SetLables(request.GetLabels());
  }
  if (request.GetMetadataParam() != NULL) {
    route_info.SetMetadataPara(*request.GetMetadataParam());
  }
  // 执行路由过滤
  RouteResult route_result;
  ReturnCode ret = service_context->GetServiceRouterChain()->DoRoute(route_info, &route_result);
  if (POLARIS_UNLIKELY(ret != kReturnOk)) {
    POLARIS_LOG(LOG_ERROR, "get one instance for service[%s/%s] with route chain retrun error:%s",
                service_key.namespace_.c_str(), service_key.name_.c_str(),
                ReturnCodeToMsg(ret).c_str());
    return ret;
  }
  // TODO 执行转发

  // 获取过滤结果
  ServiceInstances* service_instances = route_result.GetServiceInstances();

  // 负载均衡
  LoadBalancer* load_balancer = service_context->GetLoadBalancer(request.GetLoadBalanceType());
  Instance* select_instance   = NULL;
  if (load_balancer == NULL) {
    return kReturnPluginError;
  }
  ret = load_balancer->ChooseInstance(service_instances, request.GetCriteria(), select_instance);
  if (POLARIS_UNLIKELY(ret != kReturnOk)) {
    POLARIS_LOG(LOG_ERROR, "get one instance for service[%s/%s] with load balancer retrun error:%s",
                service_key.namespace_.c_str(), service_key.name_.c_str(),
                ReturnCodeToMsg(ret).c_str());
    return kReturnInstanceNotFound;
  }

  // 返回结果
  instance = *select_instance;
  return kReturnOk;
}

ReturnCode ConsumerApiImpl::GetOneInstance(ServiceContext* service_context, RouteInfo& route_info,
                                           GetOneInstanceRequestAccessor& request,
                                           InstancesResponse*& resp) {
  const ServiceKey& service_key    = route_info.GetServiceKey();
  ServiceRouterChain* router_chain = service_context->GetServiceRouterChain();
  if (!request.GetLabels().empty()) {
    route_info.SetLables(request.GetLabels());
  }
  if (request.GetMetadataParam() != NULL) {
    route_info.SetMetadataPara(*request.GetMetadataParam());
  }
  // 执行路由过滤
  RouteResult route_result;
  ReturnCode ret = router_chain->DoRoute(route_info, &route_result);
  if (ret != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "get one instance for service[%s/%s] with route chain retrun error:%s",
                service_key.namespace_.c_str(), service_key.name_.c_str(),
                ReturnCodeToMsg(ret).c_str());
    return ret;
  }
  // TODO 执行转发

  // 获取过滤结果
  ServiceInstances* service_instances = route_result.GetServiceInstances();

  Instance* instance = NULL;

  // 负载均衡
  LoadBalancer* load_balancer = service_context->GetLoadBalancer(request.GetLoadBalanceType());
  ret = load_balancer->ChooseInstance(service_instances, request.GetCriteria(), instance);
  if (ret != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "get one instance for service[%s/%s] with load balancer retrun error:%s",
                service_key.namespace_.c_str(), service_key.name_.c_str(),
                ReturnCodeToMsg(ret).c_str());
    return kReturnInstanceNotFound;
  }

  // 选取backup实例
  std::vector<Instance*> backup_instances;
  backup_instances.push_back(instance);
  GetBackupInstances(service_instances, load_balancer, request, backup_instances);

  // 返回结果
  resp = new InstancesResponse();
  InstancesResponseSetter resp_setter(*resp);
  resp_setter.SetFlowId(request.GetFlowId());
  resp_setter.SetMetadata(service_instances->GetServiceMetadata());
  resp_setter.SetServiceName(route_info.GetServiceKey().name_);
  resp_setter.SetServiceNamespace(route_info.GetServiceKey().namespace_);
  resp_setter.SetRevision(service_instances->GetServiceData()->GetRevision());
  resp_setter.SetSubset(route_result.GetSubset());
  for (size_t i = 0; i < backup_instances.size(); ++i) {
    resp_setter.AddInstance(*(backup_instances[i]));
  }
  return kReturnOk;
}

void ConsumerApiImpl::GetBackupInstances(ServiceInstances* service_instances,
                                         LoadBalancer* load_balancer,
                                         GetOneInstanceRequestAccessor& request,
                                         std::vector<Instance*>& backup_instances) {
  uint32_t target_num = request.GetBackupInstanceNum() + 1;  // 加负载均衡选的那一个
  if (target_num <= 1) {
    return;
  }

  LoadBalanceType lb_type = load_balancer->GetLoadBalanceType();  // 不从request中取，规避default
  InstancesSet* instances_set      = service_instances->GetAvailableInstances();
  std::vector<Instance*> instances = instances_set->GetInstances();
  Instance* instance               = NULL;
  ReturnCode ret                   = kReturnOk;

  // 内部ringhash, 返回节点后相邻的backup个不重复节点
  if (lb_type == kLoadBalanceTypeRingHash || lb_type == kLoadBalanceTypeL5CstHash ||
      lb_type == kLoadBalanceTypeCMurmurHash) {
    uint32_t available_num = instances.size();  // 不考虑半开
    if (target_num > available_num) {
      POLARIS_LOG(LOG_WARN, "available instance num %d is small than needed instance num %d",
                  available_num, target_num);
      target_num = available_num;  // 修正目标值
    }
    int cycle_times   = available_num;  //循环次数上限
    Criteria criteria = request.GetCriteria();

    for (int i = 1; i <= cycle_times; ++i) {
      if (backup_instances.size() >= target_num) {
        break;
      }
      criteria.replicate_index_ = i;
      ret = load_balancer->ChooseInstance(service_instances, criteria, instance);
      if (ret != kReturnOk) {
        POLARIS_LOG(LOG_ERROR, "load balancer %s choose backup instance error %d", lb_type.c_str(),
                    ret);
        return;
      }
      // 添加不重复的节点到数组
      bool repeat_flag = false;
      for (size_t j = 0; j < backup_instances.size(); ++j) {
        if (backup_instances[j]->GetId() == instance->GetId()) {
          repeat_flag = true;
          break;
        }
      }
      if (repeat_flag == false) {
        backup_instances.push_back(instance);
      }
    }
    return;
  }

  // 其它负载均衡
  std::set<Instance*> half_open_instances;
  service_instances->GetHalfOpenInstances(half_open_instances);  // 半开实例
  instance = backup_instances[0];

  uint32_t available_num = instances.size() - half_open_instances.size();
  if (target_num > available_num) {
    POLARIS_LOG(LOG_WARN, "available instance num %d is small than needed instance num %d",
                available_num, target_num);
    target_num = available_num;
  }

  // 获取一个随机数
  static __thread bool thread_local_seed_not_init = true;
  static __thread unsigned int thread_local_seed  = 0;
  if (thread_local_seed_not_init) {
    thread_local_seed_not_init = false;
    thread_local_seed          = time(NULL) ^ pthread_self();
  }
  uint32_t index = rand_r(&thread_local_seed) % instances.size();
  // 选择backup实例
  for (size_t i = 0; i < instances.size(); ++i, ++index) {
    if (backup_instances.size() >= target_num) {
      break;  // 实例数已足够
    }
    if (index == instances.size()) {
      index = 0;  // 回到起点
    }
    Instance*& item = instances[index];
    if (item->GetId() == instance->GetId() ||
        half_open_instances.find(item) != half_open_instances.end()) {
      continue;  // 实例是负载均衡器选择的实例，或是一个半开实例
    }
    backup_instances.push_back(item);
  }
}

ReturnCode ConsumerApiImpl::GetInstances(ServiceContext* service_context, RouteInfo& route_info,
                                         GetInstancesRequestAccessor& request,
                                         InstancesResponse*& resp) {
  ReturnCode ret;
  ServiceRouterChain* router_chain = service_context->GetServiceRouterChain();
  ServiceInstances* service_instances;
  std::set<std::string> open_instances_set;
  if (request.GetSkipRouteFilter()) {
    service_instances = route_info.GetServiceInstances();
    if (!request.GetIncludeCircuitBreakerInstances()) {  // 需要过滤熔断实例
      open_instances_set = service_instances->GetService()->GetCircuitBreakerOpenInstances();
    }
    route_info.SetServiceInstances(NULL);
  } else {
    if (request.GetIncludeCircuitBreakerInstances()) {
      route_info.SetIncludeCircuitBreakerInstances();
    }
    if (request.GetIncludeUnhealthyInstances()) {
      route_info.SetIncludeUnhealthyInstances();
    }
    if (request.GetMetadataParam() != NULL) {
      route_info.SetMetadataPara(*request.GetMetadataParam());
    }
    RouteResult route_result;
    if ((ret = router_chain->DoRoute(route_info, &route_result)) != kReturnOk) {
      return ret;
    }
    service_instances = route_result.GetAndClearServiceInstances();
  }
  InstancesSet* instances_set             = service_instances->GetAvailableInstances();
  const std::vector<Instance*>& instances = instances_set->GetInstances();
  if (instances.empty()) {
    delete service_instances;
    service_instances = NULL;
    return kReturnInstanceNotFound;
  }

  resp = new InstancesResponse();
  InstancesResponseSetter resp_setter(*resp);
  resp_setter.SetFlowId(request.GetFlowId());
  resp_setter.SetMetadata(service_instances->GetServiceMetadata());
  resp_setter.SetServiceName(route_info.GetServiceKey().name_);
  resp_setter.SetServiceNamespace(route_info.GetServiceKey().namespace_);
  resp_setter.SetRevision(service_instances->GetServiceData()->GetRevision());
  for (std::size_t i = 0; i < instances.size(); ++i) {
    if (open_instances_set.find(instances[i]->GetId()) != open_instances_set.end()) {
      continue;
    }
    resp_setter.AddInstance(*instances[i]);
  }
  delete service_instances;
  service_instances = NULL;
  return kReturnOk;
}

template <typename R>
inline bool CheckAndSetRequest(R& request, const char* action, Context* context) {
  // 检查请求参数
  if (request.GetServiceKey().namespace_.empty()) {
    POLARIS_LOG(LOG_ERROR, "%s failed because request's service namespace is empty", action);
    return false;
  }
  if (request.GetServiceKey().name_.empty()) {
    POLARIS_LOG(LOG_ERROR, "%s failed because request's service name is empty", action);
    return false;
  }
  // 未设置flow id和超时时间时，对其进行设置
  if (!request.HasFlowId()) {
    request.SetFlowId(Utils::GetNextSeqId());
  }
  if (!request.HasTimeout() || request.GetTimeout() <= 0) {
    request.SetTimeout(context->GetContextImpl()->GetApiDefaultTimeout());
  }
  return true;
}

ReturnCode ConsumerApi::InitService(const GetOneInstanceRequest& req) {
  Context* context = impl_->context_;
  ApiStat api_stat(context, kApiStatConsumerInitService);
  GetOneInstanceRequestAccessor request(req);
  if (POLARIS_UNLIKELY(!CheckAndSetRequest(request, __func__, context))) {
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }

  ContextImpl* context_impl = context->GetContextImpl();
  context_impl->RcuEnter();
  ServiceContext* service_context =
      context_impl->GetOrCreateServiceContext(request.GetServiceKey());
  if (POLARIS_UNLIKELY(service_context == NULL)) {
    context_impl->RcuExit();
    RECORD_THEN_RETURN(kReturnInvalidConfig);
  }

  RouteInfo route_info(request.GetServiceKey(), request.DumpSourceService());
  ReturnCode ret = ConsumerApiImpl::PrepareRouteInfo(service_context, route_info, __func__,
                                                     request.GetTimeout());
  service_context->DecrementRef();
  context_impl->RcuExit();
  RECORD_THEN_RETURN(ret);
}

ReturnCode ConsumerApi::GetOneInstance(const GetOneInstanceRequest& req, Instance& instance) {
  Context* context = impl_->context_;
  ApiStat api_stat(context, kApiStatConsumerGetOne);
  GetOneInstanceRequestAccessor request(req);
  if (POLARIS_UNLIKELY(!CheckAndSetRequest(request, __func__, context))) {
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }

  ContextImpl* context_impl = context->GetContextImpl();
  context_impl->RcuEnter();
  ServiceContext* service_context =
      context_impl->GetOrCreateServiceContext(request.GetServiceKey());
  if (POLARIS_UNLIKELY(service_context == NULL)) {
    context_impl->RcuExit();
    RECORD_THEN_RETURN(kReturnInvalidConfig);
  }

  RouteInfo route_info(request.GetServiceKey(), request.DumpSourceService());
  ReturnCode ret = ConsumerApiImpl::PrepareRouteInfo(service_context, route_info, __func__,
                                                     request.GetTimeout());
  if (POLARIS_LIKELY(ret == kReturnOk)) {
    ret = ConsumerApiImpl::GetOneInstance(service_context, route_info, request, instance);
  }
  service_context->DecrementRef();
  context_impl->RcuExit();
  RECORD_THEN_RETURN(ret);
}

ReturnCode ConsumerApi::GetOneInstance(const GetOneInstanceRequest& req, InstancesResponse*& resp) {
  ApiStat api_stat(impl_->context_, kApiStatConsumerGetOne);
  GetOneInstanceRequestAccessor request(req);
  if (!CheckAndSetRequest(request, __func__, impl_->context_)) {
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }

  ContextImpl* context_impl = impl_->context_->GetContextImpl();
  context_impl->RcuEnter();
  ServiceContext* service_context =
      context_impl->GetOrCreateServiceContext(request.GetServiceKey());
  if (service_context == NULL) {
    context_impl->RcuExit();
    RECORD_THEN_RETURN(kReturnInvalidConfig);
  }

  RouteInfo route_info(request.GetServiceKey(), request.DumpSourceService());
  ReturnCode ret = ConsumerApiImpl::PrepareRouteInfo(service_context, route_info, __func__,
                                                     request.GetTimeout());
  if (ret == kReturnOk) {
    ret = ConsumerApiImpl::GetOneInstance(service_context, route_info, request, resp);
  }
  service_context->DecrementRef();
  context_impl->RcuExit();
  RECORD_THEN_RETURN(ret);
}

ReturnCode ConsumerApi::GetInstances(const GetInstancesRequest& req, InstancesResponse*& resp) {
  ApiStat api_stat(impl_->context_, kApiStatConsumerGetBatch);
  GetInstancesRequestAccessor request(req);
  if (!CheckAndSetRequest(request, __func__, impl_->context_)) {
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }

  ContextImpl* context_impl = impl_->context_->GetContextImpl();
  context_impl->RcuEnter();
  ServiceContext* service_context =
      context_impl->GetOrCreateServiceContext(request.GetServiceKey());
  if (service_context == NULL) {
    context_impl->RcuExit();
    RECORD_THEN_RETURN(kReturnInvalidConfig);
  }

  RouteInfo route_info(request.GetServiceKey(), request.DumpSourceService());
  ReturnCode ret = ConsumerApiImpl::PrepareRouteInfo(service_context, route_info, __func__,
                                                     request.GetTimeout());
  if (ret == kReturnOk) {
    ret = ConsumerApiImpl::GetInstances(service_context, route_info, request, resp);
  }
  service_context->DecrementRef();
  context_impl->RcuExit();
  RECORD_THEN_RETURN(ret);
}

ReturnCode ConsumerApi::GetAllInstances(const GetInstancesRequest& req, InstancesResponse*& resp) {
  ApiStat api_stat(impl_->context_, kApiStatConsumerGetAll);
  GetInstancesRequestAccessor request(req);
  if (!CheckAndSetRequest(request, __func__, impl_->context_)) {
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }

  ContextImpl* context_impl     = impl_->context_->GetContextImpl();
  LocalRegistry* local_registry = impl_->context_->GetLocalRegistry();
  ServiceData* service_data     = NULL;
  context_impl->RcuEnter();
  ReturnCode ret_code = local_registry->GetServiceDataWithRef(request.GetServiceKey(),
                                                              kServiceDataInstances, service_data);
  if (ret_code != kReturnOk) {
    ServiceDataNotify* service_notify;
    ret_code = local_registry->LoadServiceDataWithNotify(
        request.GetServiceKey(), kServiceDataInstances, service_data, service_notify);
    if (ret_code == kReturnOk) {
      timespec ts = Time::CurrentTimeAddWith(request.GetTimeout());
      ret_code    = service_notify->WaitDataWithRefUtil(ts, service_data);
    }
  }
  context_impl->RcuExit();
  if (service_data == NULL) {
    RECORD_THEN_RETURN(ret_code);
  }
  if (service_data->GetDataStatus() == kDataNotFound) {
    service_data->DecrementRef();
    RECORD_THEN_RETURN(kReturnServiceNotFound);
  }
  ServiceInstances* service_instances = new ServiceInstances(service_data);
  resp                                = new InstancesResponse();
  InstancesResponseSetter resp_setter(*resp);
  resp_setter.SetFlowId(request.GetFlowId());
  resp_setter.SetMetadata(service_instances->GetServiceMetadata());
  resp_setter.SetServiceName(request.GetServiceKey().name_);
  resp_setter.SetServiceNamespace(request.GetServiceKey().namespace_);
  resp_setter.SetRevision(service_instances->GetServiceData()->GetRevision());
  std::map<std::string, Instance*>& instances = service_instances->GetInstances();
  for (std::map<std::string, Instance*>::iterator it = instances.begin(); it != instances.end();
       ++it) {
    resp_setter.AddInstance(*it->second);
  }
  std::set<Instance*>& isolate_instances = service_instances->GetIsolateInstances();
  for (std::set<Instance*>::iterator it = isolate_instances.begin(); it != isolate_instances.end();
       ++it) {
    resp_setter.AddInstance(**it);
  }
  delete service_instances;
  return kReturnOk;
}

ReturnCode ConsumerApi::AsyncGetOneInstance(const GetOneInstanceRequest& req,
                                            InstancesFuture*& future) {
  ApiStat* api_stat = new ApiStat(impl_->context_, kApiStatConsumerAsyncGetOne);
  GetOneInstanceRequestAccessor request(req);
  if (!CheckAndSetRequest(request, __func__, impl_->context_)) {
    api_stat->Record(kReturnInvalidArgument);
    delete api_stat;
    return kReturnInvalidArgument;
  }
  ContextImpl* context_impl = impl_->context_->GetContextImpl();
  context_impl->RcuEnter();
  ServiceContext* service_context =
      context_impl->GetOrCreateServiceContext(request.GetServiceKey());
  if (service_context == NULL) {
    api_stat->Record(kReturnInvalidConfig);
    delete api_stat;
    context_impl->RcuExit();
    return kReturnInvalidConfig;
  }
  future = InstancesFutureImpl::CreateInstancesFuture(api_stat, context_impl, service_context,
                                                      request.Dump());
  context_impl->RcuExit();
  return kReturnOk;
}

ReturnCode ConsumerApi::AsyncGetInstances(const GetInstancesRequest& req,
                                          InstancesFuture*& future) {
  ApiStat* api_stat = new ApiStat(impl_->context_, kApiStatConsumerAsyncGetBatch);
  GetInstancesRequestAccessor request(req);
  if (!CheckAndSetRequest(request, __func__, impl_->context_)) {
    api_stat->Record(kReturnInvalidArgument);
    delete api_stat;
    return kReturnInvalidArgument;
  }
  ContextImpl* context_impl = impl_->context_->GetContextImpl();
  context_impl->RcuEnter();
  ServiceContext* service_context =
      context_impl->GetOrCreateServiceContext(request.GetServiceKey());
  if (service_context == NULL) {
    api_stat->Record(kReturnInvalidConfig);
    delete api_stat;
    context_impl->RcuExit();
    return kReturnInvalidConfig;
  }
  future = InstancesFutureImpl::CreateInstancesFuture(api_stat, context_impl, service_context,
                                                      request.Dump());
  context_impl->RcuExit();
  return kReturnOk;
}

ReturnCode ConsumerApi::UpdateServiceCallResult(const ServiceCallResult& req) {
  ApiStat api_stat(impl_->context_, kApiStatConsumerCallResult);
  ServiceCallResultGetter result_getter(req);
  // 检查请求参数
  if (result_getter.GetServiceName().empty()) {
    POLARIS_LOG(LOG_ERROR, "%s failed because request's service name is empty", __func__);
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }

  // 设置Gauge
  InstanceGauge instance_gauge;
  instance_gauge.service_namespace   = result_getter.GetServiceNamespace();
  instance_gauge.service_name        = result_getter.GetServiceName();
  instance_gauge.instance_id         = result_getter.GetInstanceId();
  instance_gauge.call_daley          = result_getter.GetDelay();
  instance_gauge.call_ret_code       = result_getter.GetRetCode();
  instance_gauge.call_ret_status     = result_getter.GetRetStatus();
  instance_gauge.source_service_key  = result_getter.GetSource();
  instance_gauge.subset_             = result_getter.GetSubset();
  instance_gauge.labels_             = result_getter.GetLabels();
  instance_gauge.locality_aware_info = result_getter.GetLocalityAwareInfo();

  ReturnCode ret_code;
  if (instance_gauge.instance_id.empty()) {
    if (result_getter.GetHost().empty() || result_getter.GetPort() <= 0) {
      POLARIS_LOG(LOG_ERROR, "%s failed because InstanceId and Host:Port is empty", __func__);
      RECORD_THEN_RETURN(kReturnInvalidArgument);
    }
    // 通过Host:Port获取服务实例ID
    CacheManager* cache_manager = impl_->context_->GetContextImpl()->GetCacheManager();
    ServiceKey service_key = {result_getter.GetServiceNamespace(), result_getter.GetServiceName()};
    ret_code               = cache_manager->GetInstanceId(service_key, result_getter.GetHost(),
                                            result_getter.GetPort(), instance_gauge.instance_id);
    if (ret_code != kReturnOk) {
      RECORD_THEN_RETURN(ret_code);
    }
  }
  ret_code = impl_->UpdateServiceCallResult(impl_->context_, instance_gauge);
  RECORD_THEN_RETURN(ret_code);
}

ReturnCode ConsumerApi::GetRouteRuleKeys(const ServiceKey& service_key, uint64_t timeout,
                                         const std::set<std::string>*& keys) {
  ContextImpl* context_impl     = impl_->context_->GetContextImpl();
  LocalRegistry* local_registry = impl_->context_->GetLocalRegistry();
  ServiceData* service_data     = NULL;
  context_impl->RcuEnter();
  ReturnCode ret_code =
      local_registry->GetServiceDataWithRef(service_key, kServiceDataRouteRule, service_data);
  if (ret_code != kReturnOk) {
    ServiceDataNotify* service_notify;
    ret_code = local_registry->LoadServiceDataWithNotify(service_key, kServiceDataRouteRule,
                                                         service_data, service_notify);
    if (ret_code == kReturnOk) {
      timespec ts = Time::CurrentTimeAddWith(timeout);
      ret_code    = service_notify->WaitDataWithRefUtil(ts, service_data);
    }
  }
  context_impl->RcuExit();
  if (service_data == NULL) {
    return ret_code;
  }

  if (service_data->GetDataStatus() == kDataNotFound) {
    service_data->DecrementRef();
    return kReturnServiceNotFound;
  }
  ServiceRouteRule route_rule(service_data);
  keys = &route_rule.GetKeys();
  return kReturnOk;
}

#endif  // ONLY_RATE_LIMIT

ReturnCode ConsumerApiImpl::UpdateServiceCallResult(Context* context, const InstanceGauge& gauge) {
  ServiceKey service_key    = {gauge.service_namespace, gauge.service_name};
  ContextImpl* context_impl = context->GetContextImpl();
  context_impl->RcuEnter();
  ServiceContext* service_context = context_impl->GetOrCreateServiceContext(service_key);
  if (service_context == NULL) {
    POLARIS_LOG(LOG_ERROR,
                "update service call result failed because context of service[%s/%s] not exist",
                service_key.namespace_.c_str(), service_key.name_.c_str());
    context_impl->RcuExit();
    return kReturnInvalidArgument;
  }
  // 执行上报统计插件
  StatReporter* stat_reporter = context_impl->GetStatReporter();
  stat_reporter->ReportStat(gauge);

  // TODO 执行动态权重调整
  WeightAdjuster* weight_adjuster = service_context->GetWeightAdjuster();
  bool need_adjust                = false;
  weight_adjuster->RealTimeAdjustDynamicWeight(gauge, need_adjust);
  // 权重调整插件有触发权重更新，则更新到本地缓存

  // LocalityAwareLoadBalancer Feedback
  if (gauge.locality_aware_info != 0) {
    // locality_aware_info构造时默认为0，LA填写的均非0
    LoadBalancer* load_balancer = service_context->GetLoadBalancer(kLoadBalanceTypeLocalityAware);
    if (load_balancer != NULL) {
      LocalityAwareLoadBalancer* locality_aware_load_balancer =
          dynamic_cast<LocalityAwareLoadBalancer*>(load_balancer);
      if (locality_aware_load_balancer != NULL) {
        FeedbackInfo info;
        info.call_daley          = gauge.call_daley;
        info.instance_id         = gauge.instance_id;
        info.locality_aware_info = gauge.locality_aware_info;
        locality_aware_load_balancer->Feedback(info);
      }
    }
  }
  // 执行熔断插件
  CircuitBreakerChain* circuit_breaker_chain = service_context->GetCircuitBreakerChain();
  circuit_breaker_chain->RealTimeCircuitBreak(gauge);
  service_context->DecrementRef();
  context_impl->RcuExit();
  return kReturnOk;
}

ReturnCode ConsumerApiImpl::GetSystemServer(Context* context, const ServiceKey& service_key,
                                            const Criteria& criteria, Instance*& instance,
                                            uint64_t timeout, const std::string& protocol) {
  if (service_key.name_.empty() || service_key.namespace_.empty()) {
    return kReturnSystemServiceNotConfigured;
  }
  ContextImpl* context_impl = context->GetContextImpl();
  context_impl->RcuEnter();
  ServiceContext* service_context = context->GetOrCreateServiceContext(service_key);
  if (service_context == NULL) {
    context_impl->RcuExit();
    return kReturnInvalidConfig;
  }
  RouteInfo route_info(service_key, NULL);
  MetadataRouterParam metadata_param;
  metadata_param.metadata_.insert(std::make_pair("protocol", protocol));
  route_info.SetMetadataPara(metadata_param);
  ServiceRouterChain* service_route_chain = service_context->GetServiceRouterChain();
  ReturnCode ret = service_route_chain->PrepareRouteInfo(route_info, timeout);
  if (ret != kReturnOk) {
    service_context->DecrementRef();
    context_impl->RcuExit();
    return ret;
  }
  RouteResult route_result;
  if ((ret = service_route_chain->DoRoute(route_info, &route_result)) != kReturnOk) {
    service_context->DecrementRef();
    context_impl->RcuExit();
    return ret;
  }
  ServiceInstances* service_instances = route_result.GetServiceInstances();
  LoadBalancer* load_balancer         = service_context->GetLoadBalancer(
      criteria.hash_string_.empty() ? kLoadBalanceTypeDefaultConfig : kLoadBalanceTypeRingHash);
  Instance* select_instance = NULL;
  if ((ret = load_balancer->ChooseInstance(service_instances, criteria, select_instance)) ==
      kReturnOk) {
    instance = new Instance(*select_instance);
  }
  context_impl->RcuExit();
  service_context->DecrementRef();
  return ret;
}

}  // namespace polaris
