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

#include <v1/code.pb.h>
#include <v1/routing.pb.h>
#include <iosfwd>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "cache/cache_manager.h"
#include "context/context_impl.h"
#include "logger.h"
#include "model/model_impl.h"
#include "monitor/api_stat.h"
#include "plugin/load_balancer/locality_aware/locality_aware.h"
#include "polaris/config.h"
#include "polaris/defs.h"
#include "polaris/plugin.h"
#include "utils/fork.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

#ifndef ONLY_RATE_LIMIT
ConsumerApi::ConsumerApi(ConsumerApiImpl* impl) { impl_ = impl; }

ConsumerApi::~ConsumerApi() {
  if (impl_ != nullptr) {
    delete impl_;
    impl_ = nullptr;
  }
}

ConsumerApi* ConsumerApi::Create(Context* context) {
  if (context == nullptr) {
    POLARIS_LOG(LOG_ERROR, "create consumer api failed because context is null");
    return nullptr;
  }
  if (context->GetContextMode() != kPrivateContext && context->GetContextMode() != kShareContext &&
      context->GetContextMode() != kLimitContext) {
    POLARIS_LOG(LOG_ERROR, "create consumer api failed because context is init with error mode");
    return nullptr;
  }
  ConsumerApiImpl* api_impl = new ConsumerApiImpl(context);
  return new ConsumerApi(api_impl);
}

ConsumerApi* ConsumerApi::CreateFromConfig(Config* config) {
  if (config == nullptr) {
    POLARIS_LOG(LOG_WARN, "create consumer api failed because parameter config is null");
    return nullptr;
  }
  Context* context = Context::Create(config, kPrivateContext);
  if (context == nullptr) {
    return nullptr;
  }
  return ConsumerApi::Create(context);
}

static ConsumerApi* CreateWithConfig(Config* config, const std::string& err_msg) {
  if (config == nullptr) {
    POLARIS_LOG(LOG_ERROR, "init config with error: %s", err_msg.c_str());
    return nullptr;
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

InstancesFuture::Impl::Impl(const ServiceKey& service_key, ApiStat* api_stat, ContextImpl* context_impl,
                            ServiceInfo* source_service_info)
    : api_stat_(api_stat),
      context_impl_(context_impl),
      source_service_info_(source_service_info == nullptr ? nullptr : new ServiceInfo(*source_service_info)),
      route_info_(service_key, source_service_info_) {
  one_instance_req_ = nullptr;
  instances_req_ = nullptr;
  route_info_notify_ = nullptr;
  request_timeout_ = 0;
}

InstancesFuture::Impl::~Impl() {
  if (api_stat_ != nullptr) {
    delete api_stat_;
    api_stat_ = nullptr;
  }
  if (one_instance_req_ != nullptr) {
    delete one_instance_req_;
    one_instance_req_ = nullptr;
  }
  if (instances_req_ != nullptr) {
    delete instances_req_;
    instances_req_ = nullptr;
  }
  if (route_info_notify_ != nullptr) {
    delete route_info_notify_;
    route_info_notify_ = nullptr;
  }
  if (source_service_info_ != nullptr) {
    delete source_service_info_;
    source_service_info_ = nullptr;
  }
}

InstancesFuture* InstancesFuture::Impl::CreateInstancesFuture(ApiStat* api_stat, ContextImpl* context_impl,
                                                              ServiceContext* service_context,
                                                              GetOneInstanceRequest::Impl& req_impl) {
  InstancesFuture::Impl* impl =
      new InstancesFuture::Impl(req_impl.service_key_, api_stat, context_impl, req_impl.source_service_.get());
  impl->one_instance_req_ = req_impl.Dump();
  impl->request_timeout_ = req_impl.timeout_.Value();
  ServiceRouterChain* router_chain = service_context->GetServiceRouterChain();
  impl->route_info_notify_ = router_chain->PrepareRouteInfoWithNotify(impl->route_info_);
  return new InstancesFuture(impl);
}

InstancesFuture* InstancesFuture::Impl::CreateInstancesFuture(ApiStat* api_stat, ContextImpl* context_impl,
                                                              ServiceContext* service_context,
                                                              GetInstancesRequest::Impl& req_impl) {
  InstancesFuture::Impl* impl =
      new InstancesFuture::Impl(req_impl.service_key_, api_stat, context_impl, req_impl.source_service_.get());
  impl->instances_req_ = req_impl.Dump();
  impl->request_timeout_ = req_impl.timeout_.Value();
  ServiceRouterChain* router_chain = service_context->GetServiceRouterChain();
  impl->route_info_notify_ = router_chain->PrepareRouteInfoWithNotify(impl->route_info_);
  return new InstancesFuture(impl);
}

ReturnCode InstancesFuture::Impl::CheckReady() {
  if (route_info_notify_ == nullptr || route_info_notify_->IsDataReady(false)) {
    return kReturnOk;
  } else {  // 查询未就绪则等待
    timespec ts = Time::SteadyTimeAdd(0);
    return route_info_notify_->WaitData(ts);
  }
}

InstancesFuture::InstancesFuture(InstancesFuture::Impl* impl) : impl_(impl) {}

InstancesFuture::~InstancesFuture() {
  if (impl_ != nullptr) {
    impl_->DecrementRef();
    impl_ = nullptr;
  }
}

bool InstancesFuture::IsDone(bool use_disk_data) {
  return impl_->route_info_notify_ == nullptr || impl_->route_info_notify_->IsDataReady(use_disk_data);
}

ReturnCode InstancesFuture::Get(uint64_t wait_time, InstancesResponse*& result) {
  ReturnCode ret = kReturnOk;
  impl_->context_impl_->RcuEnter();
  if (impl_->route_info_notify_ != nullptr) {  // 还有数据未就绪
    bool use_disk_data = false;
    if (!impl_->route_info_notify_->IsDataReady(use_disk_data)) {  // 查询未就绪则等待
      timespec ts = Time::SteadyTimeAdd(wait_time);
      ret = impl_->route_info_notify_->WaitData(ts);
    }
    // 查询得到数据就绪，或等待数据就绪成功。或等待失败但存在磁盘数据
    use_disk_data = true;
    if (ret == kReturnOk || impl_->route_info_notify_->IsDataReady(use_disk_data)) {
      ret = impl_->route_info_notify_->SetDataToRouteInfo(impl_->route_info_);
      delete impl_->route_info_notify_;  // 数据准备就绪之后释放notify
      impl_->route_info_notify_ = nullptr;
    }
  }
  const ServiceKey& service_key = impl_->route_info_.GetServiceKey();
  ServiceContext* service_context = impl_->context_impl_->GetServiceContext(service_key);
  if (service_context == nullptr) {
    ret = kReturnInvalidConfig;
  }
  if (ret == kReturnOk) {
    if (impl_->one_instance_req_ != nullptr) {
      ret = ConsumerApiImpl::GetOneInstance(service_context, impl_->route_info_, *impl_->one_instance_req_, result);
    } else {
      ret = ConsumerApiImpl::GetInstances(service_context, impl_->route_info_, *impl_->instances_req_, result);
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

ConsumerApiImpl::ConsumerApiImpl(Context* context) : context_(context) { srand(time(nullptr)); }

ConsumerApiImpl::~ConsumerApiImpl() {
  if (context_ != nullptr && context_->GetContextMode() == kPrivateContext) {
    delete context_;
  }
}

ReturnCode ConsumerApiImpl::PrepareRouteInfo(ServiceContext* service_context, RouteInfo& route_info, const char* action,
                                             uint64_t request_timeout) {
  // 准备路由过滤数据
  ServiceData* instances = service_context->GetInstances();
  if (instances != nullptr) {
    route_info.SetServiceInstances(new ServiceInstances(instances));
  }
  ServiceData* routings = service_context->GetRoutings();
  if (routings != nullptr) {
    route_info.SetServiceRouteRule(new ServiceRouteRule(routings));
  }
  route_info.SetCircuitBreakerVersion(service_context->GetCircuitBreakerVersion());
  ServiceRouterChain* router_chain = service_context->GetServiceRouterChain();
  ReturnCode ret_code = router_chain->PrepareRouteInfo(route_info, request_timeout);
  if (ret_code != kReturnOk) {
    const ServiceKey& service_key = route_info.GetServiceKey();
    POLARIS_LOG(LOG_ERROR, "%s prepare route info for service[%s/%s] with error:%s", action,
                service_key.namespace_.c_str(), service_key.name_.c_str(), ReturnCodeToMsg(ret_code).c_str());
  }
  return ret_code;
}

ReturnCode ConsumerApiImpl::GetOneInstance(ServiceContext* service_context, RouteInfo& route_info,
                                           GetOneInstanceRequest::Impl& request, Instance& instance) {
  if (!request.GetLabels().empty()) {
    route_info.SetLables(request.GetLabels());
  }
  if (request.metadata_param_ != nullptr) {
    route_info.SetMetadataPara(*request.metadata_param_);
  }
  // 执行路由过滤
  RouteResult route_result;
  ReturnCode ret = service_context->DoRoute(route_info, &route_result);
  if (POLARIS_UNLIKELY(ret != kReturnOk)) {
    const ServiceKey& service_key = route_info.GetServiceKey();
    POLARIS_LOG(LOG_ERROR, "get one instance for service[%s/%s] with route chain retrun error:%s",
                service_key.namespace_.c_str(), service_key.name_.c_str(), ReturnCodeToMsg(ret).c_str());
    return ret;
  }
  // TODO 执行转发

  // 获取过滤结果
  ServiceInstances* service_instances = route_info.GetServiceInstances();

  // 负载均衡
  LoadBalancer* load_balancer = service_context->GetLoadBalancer(request.load_balance_type_);
  if (load_balancer == nullptr) {
    return kReturnPluginError;
  }
  Instance* select_instance = nullptr;
  ret = load_balancer->ChooseInstance(service_instances, request.criteria_, select_instance);
  if (POLARIS_UNLIKELY(ret != kReturnOk)) {
    const ServiceKey& service_key = route_info.GetServiceKey();
    POLARIS_LOG(LOG_ERROR, "get one instance for service[%s/%s] with load balancer return error:%s",
                service_key.namespace_.c_str(), service_key.name_.c_str(), ReturnCodeToMsg(ret).c_str());
    return kReturnInstanceNotFound;
  }

  // 返回结果
  instance = *select_instance;
  if (select_instance->GetLocalityAwareInfo() > 0) {
    delete select_instance;
  }
  return kReturnOk;
}

ReturnCode ConsumerApiImpl::GetOneInstance(ServiceContext* service_context, RouteInfo& route_info,
                                           GetOneInstanceRequest::Impl& req_impl, InstancesResponse*& resp) {
  if (!req_impl.GetLabels().empty()) {
    route_info.SetLables(req_impl.GetLabels());
  }
  if (req_impl.metadata_param_ != nullptr) {
    route_info.SetMetadataPara(*req_impl.metadata_param_);
  }
  // 执行路由过滤
  RouteResult route_result;
  ReturnCode ret = service_context->DoRoute(route_info, &route_result);
  if (ret != kReturnOk) {
    const ServiceKey& service_key = route_info.GetServiceKey();
    POLARIS_LOG(LOG_ERROR, "get one instance for service[%s/%s] with route chain retrun error:%s",
                service_key.namespace_.c_str(), service_key.name_.c_str(), ReturnCodeToMsg(ret).c_str());
    return ret;
  }
  // TODO 执行转发

  // 获取过滤结果
  ServiceInstances* service_instances = route_info.GetServiceInstances();

  Instance* instance = nullptr;

  // 负载均衡
  LoadBalancer* load_balancer = service_context->GetLoadBalancer(req_impl.load_balance_type_);
  if (load_balancer == nullptr) {
    return kReturnPluginError;
  }
  ret = load_balancer->ChooseInstance(service_instances, req_impl.criteria_, instance);
  if (ret != kReturnOk) {
    const ServiceKey& service_key = route_info.GetServiceKey();
    POLARIS_LOG(LOG_ERROR, "get one instance for service[%s/%s] with load balancer retrun error:%s",
                service_key.namespace_.c_str(), service_key.name_.c_str(), ReturnCodeToMsg(ret).c_str());
    return kReturnInstanceNotFound;
  }

  // 选取backup实例
  std::vector<Instance*> backup_instances;
  backup_instances.push_back(instance);
  GetBackupInstances(service_instances, load_balancer, req_impl.backup_instance_num_, req_impl.criteria_,
                     backup_instances);

  // 返回结果
  resp = new InstancesResponse();
  InstancesResponse::Impl& resp_impl = resp->GetImpl();
  resp_impl.flow_id_ = req_impl.flow_id_.Value();
  resp_impl.metadata_ = service_instances->GetServiceMetadata();
  resp_impl.service_name_ = route_info.GetServiceKey().name_;
  resp_impl.service_namespace_ = route_info.GetServiceKey().namespace_;
  resp_impl.revision_ = service_instances->GetServiceData()->GetRevision();
  resp_impl.subset_ = route_result.GetSubset();
  for (auto& instance : backup_instances) {
    resp_impl.instances_.push_back(*instance);
    if (instance->GetLocalityAwareInfo() > 0) {
      delete instance;
    }
  }
  return kReturnOk;
}

void ConsumerApiImpl::GetBackupInstances(ServiceInstances* service_instances, LoadBalancer* load_balancer,
                                         uint32_t backup_instance_num, const Criteria& criteria,
                                         std::vector<Instance*>& backup_instances) {
  uint32_t target_num = backup_instance_num + 1;  // 加负载均衡选的那一个
  if (target_num <= 1) {
    return;
  }

  LoadBalanceType lb_type = load_balancer->GetLoadBalanceType();  // 不从request中取，规避default
  InstancesSet* instances_set = service_instances->GetAvailableInstances();
  std::vector<Instance*> instances = instances_set->GetInstances();
  Instance* instance = nullptr;
  ReturnCode ret = kReturnOk;

  // 内部ringhash, 返回节点后相邻的backup个不重复节点
  if (lb_type == kLoadBalanceTypeRingHash || lb_type == kLoadBalanceTypeL5CstHash ||
      lb_type == kLoadBalanceTypeCMurmurHash) {
    uint32_t available_num = instances.size();  // 不考虑半开
    if (target_num > available_num) {
      POLARIS_LOG(LOG_WARN, "available instance num %d is small than needed instance num %d", available_num,
                  target_num);
      target_num = available_num;  // 修正目标值
    }
    int cycle_times = available_num;  //循环次数上限
    Criteria criteria_tmp = criteria;

    for (int i = 1; i <= cycle_times; ++i) {
      if (backup_instances.size() >= target_num) {
        break;
      }
      criteria_tmp.replicate_index_ = i;
      ret = load_balancer->ChooseInstance(service_instances, criteria_tmp, instance);
      if (ret != kReturnOk) {
        POLARIS_LOG(LOG_ERROR, "load balancer %s choose backup instance error %d", lb_type.c_str(), ret);
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
    POLARIS_LOG(LOG_WARN, "available instance num %d is small than needed instance num %d", available_num, target_num);
    target_num = available_num;
  }

  // 获取一个随机数
  static __thread bool thread_local_seed_not_init = true;
  static __thread unsigned int thread_local_seed = 0;
  if (thread_local_seed_not_init) {
    thread_local_seed_not_init = false;
    thread_local_seed = time(nullptr) ^ pthread_self();
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
    if (item->GetId() == instance->GetId() || half_open_instances.find(item) != half_open_instances.end()) {
      continue;  // 实例是负载均衡器选择的实例，或是一个半开实例
    }
    backup_instances.push_back(item);
  }
}

ReturnCode ConsumerApiImpl::GetInstances(ServiceContext* service_context, RouteInfo& route_info,
                                         GetInstancesRequest::Impl& req_impl, InstancesResponse*& resp) {
  ReturnCode ret;
  ServiceRouterChain* router_chain = service_context->GetServiceRouterChain();
  ServiceInstances* service_instances;
  std::set<std::string> open_instances_set;
  if (req_impl.GetSkipRouteFilter()) {
    service_instances = route_info.GetServiceInstances();
    if (!req_impl.GetIncludeCircuitBreakerInstances()) {  // 需要过滤熔断实例
      open_instances_set = service_instances->GetService()->GetCircuitBreakerOpenInstances();
    }
  } else {
    if (req_impl.GetIncludeCircuitBreakerInstances()) {
      route_info.SetIncludeCircuitBreakerInstances();
    }
    if (req_impl.GetIncludeUnhealthyInstances()) {
      route_info.SetIncludeUnhealthyInstances();
    }
    if (req_impl.metadata_param_ != nullptr) {
      route_info.SetMetadataPara(*req_impl.metadata_param_);
    }
    RouteResult route_result;
    if ((ret = router_chain->DoRoute(route_info, &route_result)) != kReturnOk) {
      return ret;
    }
    service_instances = route_info.GetServiceInstances();
  }
  InstancesSet* instances_set = service_instances->GetAvailableInstances();
  const std::vector<Instance*>& instances = instances_set->GetInstances();
  if (instances.empty()) {
    return kReturnInstanceNotFound;
  }

  resp = new InstancesResponse();
  InstancesResponse::Impl& resp_impl = resp->GetImpl();
  resp_impl.flow_id_ = req_impl.flow_id_.Value();
  resp_impl.metadata_ = service_instances->GetServiceMetadata();
  resp_impl.service_name_ = route_info.GetServiceKey().name_;
  resp_impl.service_namespace_ = route_info.GetServiceKey().namespace_;
  resp_impl.revision_ = service_instances->GetServiceData()->GetRevision();
  for (std::size_t i = 0; i < instances.size(); ++i) {
    if (open_instances_set.find(instances[i]->GetId()) == open_instances_set.end()) {
      resp_impl.instances_.push_back(*instances[i]);
    }
  }
  return kReturnOk;
}

template <typename R>
inline bool CheckAndSetRequest(R& request, const char* action, Context* context) {
  // 检查请求参数
  if (request.service_key_.namespace_.empty()) {
    POLARIS_LOG(LOG_ERROR, "%s failed because request's service namespace is empty", action);
    return false;
  }
  if (request.service_key_.name_.empty()) {
    POLARIS_LOG(LOG_ERROR, "%s failed because request's service name is empty", action);
    return false;
  }
  // 未设置flow id和超时时间时，对其进行设置
  if (!request.flow_id_.HasValue()) {
    request.flow_id_ = Utils::GetNextSeqId();
  }
  if (!request.timeout_.HasValue() || request.timeout_.Value() <= 0) {
    request.timeout_ = context->GetContextImpl()->GetApiDefaultTimeout();
  }
  return true;
}

ReturnCode ConsumerApi::InitService(const GetOneInstanceRequest& req) {
  Context* context = impl_->GetContext();
  ContextImpl* context_impl = context->GetContextImpl();
  ApiStat api_stat(context_impl, kApiStatConsumerInitService);
  GetOneInstanceRequest::Impl& req_impl = req.GetImpl();
  if (POLARIS_UNLIKELY(!CheckAndSetRequest(req_impl, __func__, context))) {
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }

  POLARIS_FORK_CHECK()

  context_impl->RcuEnter();
  ServiceContext* service_context = context_impl->GetServiceContext(req_impl.service_key_);
  if (POLARIS_UNLIKELY(service_context == nullptr)) {
    context_impl->RcuExit();
    RECORD_THEN_RETURN(kReturnInvalidConfig);
  }

  RouteInfo route_info(req_impl.service_key_, req_impl.source_service_.get());
  ReturnCode ret = ConsumerApiImpl::PrepareRouteInfo(service_context, route_info, __func__, req_impl.timeout_.Value());

  context_impl->RcuExit();
  RECORD_THEN_RETURN(ret);
}

ReturnCode ConsumerApi::GetServiceRouteRule(const ServiceKey& service_key, uint64_t timeout, std::string& json_string) {
  ContextImpl* context_impl = impl_->GetContext()->GetContextImpl();
  POLARIS_FORK_CHECK()

  LocalRegistry* local_registry = impl_->GetContext()->GetLocalRegistry();
  ServiceData* service_data = nullptr;
  context_impl->RcuEnter();
  ReturnCode ret_code = local_registry->GetServiceDataWithRef(service_key, kServiceDataRouteRule, service_data);
  if (ret_code != kReturnOk) {
    ServiceDataNotify* service_notify = nullptr;
    ret_code =
        local_registry->LoadServiceDataWithNotify(service_key, kServiceDataRouteRule, service_data, service_notify);
    if (ret_code == kReturnOk) {
      timespec ts = Time::SteadyTimeAdd(timeout);
      ret_code = service_notify->WaitDataWithRefUtil(ts, service_data);
    }
  }
  context_impl->RcuExit();
  if (service_data == nullptr) {
    return ret_code;
  }

  if (service_data->GetDataStatus() == kDataNotFound) {
    service_data->DecrementRef();
    return kReturnServiceNotFound;
  }
  json_string = service_data->ToJsonString();
  service_data->DecrementRef();
  return kReturnOk;
}

ReturnCode ConsumerApi::GetOneInstance(const GetOneInstanceRequest& req, Instance& instance) {
  Context* context = impl_->GetContext();
  ContextImpl* context_impl = context->GetContextImpl();
  ApiStat api_stat(context_impl, kApiStatConsumerGetOne);
  GetOneInstanceRequest::Impl& req_impl = req.GetImpl();
  if (POLARIS_UNLIKELY(!CheckAndSetRequest(req_impl, __func__, context))) {
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }

  POLARIS_FORK_CHECK()

  context_impl->RcuEnter();
  ServiceContext* service_context = context_impl->GetServiceContext(req_impl.service_key_);
  if (POLARIS_UNLIKELY(service_context == nullptr)) {
    context_impl->RcuExit();
    RECORD_THEN_RETURN(kReturnInvalidConfig);
  }

  RouteInfo route_info(req_impl.service_key_, req_impl.source_service_.get());
  ReturnCode ret = ConsumerApiImpl::PrepareRouteInfo(service_context, route_info, __func__, req_impl.timeout_.Value());
  if (POLARIS_LIKELY(ret == kReturnOk)) {
    ret = ConsumerApiImpl::GetOneInstance(service_context, route_info, req_impl, instance);
  }

  context_impl->RcuExit();
  RECORD_THEN_RETURN(ret);
}

ReturnCode ConsumerApi::GetOneInstance(const GetOneInstanceRequest& req, InstancesResponse*& resp) {
  Context* context = impl_->GetContext();
  ContextImpl* context_impl = context->GetContextImpl();
  ApiStat api_stat(context_impl, kApiStatConsumerGetOne);
  GetOneInstanceRequest::Impl& req_impl = req.GetImpl();
  if (!CheckAndSetRequest(req_impl, __func__, context)) {
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }

  POLARIS_FORK_CHECK()

  context_impl->RcuEnter();
  ServiceContext* service_context = context_impl->GetServiceContext(req_impl.service_key_);
  if (service_context == nullptr) {
    context_impl->RcuExit();
    RECORD_THEN_RETURN(kReturnInvalidConfig);
  }

  RouteInfo route_info(req_impl.service_key_, req_impl.source_service_.get());
  ReturnCode ret = ConsumerApiImpl::PrepareRouteInfo(service_context, route_info, __func__, req_impl.timeout_.Value());
  if (ret == kReturnOk) {
    ret = ConsumerApiImpl::GetOneInstance(service_context, route_info, req_impl, resp);
  }

  context_impl->RcuExit();
  RECORD_THEN_RETURN(ret);
}

ReturnCode ConsumerApi::GetInstances(const GetInstancesRequest& req, InstancesResponse*& resp) {
  Context* context = impl_->GetContext();
  ContextImpl* context_impl = context->GetContextImpl();
  ApiStat api_stat(context_impl, kApiStatConsumerGetBatch);
  GetInstancesRequest::Impl& req_impl = req.GetImpl();
  if (!CheckAndSetRequest(req_impl, __func__, context)) {
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }

  POLARIS_FORK_CHECK()

  context_impl->RcuEnter();
  ServiceContext* service_context = context_impl->GetServiceContext(req_impl.service_key_);
  if (service_context == nullptr) {
    context_impl->RcuExit();
    RECORD_THEN_RETURN(kReturnInvalidConfig);
  }

  RouteInfo route_info(req_impl.service_key_, req_impl.source_service_.get());
  ReturnCode ret = ConsumerApiImpl::PrepareRouteInfo(service_context, route_info, __func__, req_impl.timeout_.Value());
  if (ret == kReturnOk) {
    ret = ConsumerApiImpl::GetInstances(service_context, route_info, req_impl, resp);
  }
  context_impl->RcuExit();
  RECORD_THEN_RETURN(ret);
}

ReturnCode ConsumerApi::GetAllInstances(const GetInstancesRequest& req, InstancesResponse*& resp) {
  Context* context = impl_->GetContext();
  ContextImpl* context_impl = context->GetContextImpl();
  ApiStat api_stat(context_impl, kApiStatConsumerGetAll);
  GetInstancesRequest::Impl& req_impl = req.GetImpl();
  if (!CheckAndSetRequest(req_impl, __func__, context)) {
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }

  POLARIS_FORK_CHECK()

  LocalRegistry* local_registry = context->GetLocalRegistry();
  ServiceData* service_data = nullptr;
  context_impl->RcuEnter();
  ReturnCode ret_code =
      local_registry->GetServiceDataWithRef(req_impl.service_key_, kServiceDataInstances, service_data);
  if (ret_code != kReturnOk) {
    ServiceDataNotify* service_notify;
    ret_code = local_registry->LoadServiceDataWithNotify(req_impl.service_key_, kServiceDataInstances, service_data,
                                                         service_notify);
    if (ret_code == kReturnOk) {
      timespec ts = Time::SteadyTimeAdd(req_impl.timeout_.Value());
      ret_code = service_notify->WaitDataWithRefUtil(ts, service_data);
    }
  }
  context_impl->RcuExit();
  if (service_data == nullptr) {
    RECORD_THEN_RETURN(ret_code);
  }
  if (service_data->GetDataStatus() == kDataNotFound) {
    service_data->DecrementRef();
    RECORD_THEN_RETURN(kReturnServiceNotFound);
  }
  ServiceInstances service_instances(service_data);
  resp = new InstancesResponse();
  InstancesResponse::Impl& resp_impl = resp->GetImpl();
  resp_impl.flow_id_ = req_impl.flow_id_.Value();
  resp_impl.metadata_ = service_instances.GetServiceMetadata();
  resp_impl.service_name_ = req_impl.service_key_.name_;
  resp_impl.service_namespace_ = req_impl.service_key_.namespace_;
  resp_impl.revision_ = service_instances.GetServiceData()->GetRevision();
  std::map<std::string, Instance*>& instances = service_instances.GetInstances();
  for (std::map<std::string, Instance*>::iterator it = instances.begin(); it != instances.end(); ++it) {
    resp_impl.instances_.push_back(*it->second);
  }
  std::set<Instance*>& isolate_instances = service_instances.GetIsolateInstances();
  for (std::set<Instance*>::iterator it = isolate_instances.begin(); it != isolate_instances.end(); ++it) {
    resp_impl.instances_.push_back(**it);
  }
  service_data->DecrementRef();
  return kReturnOk;
}

ReturnCode ConsumerApi::AsyncGetOneInstance(const GetOneInstanceRequest& req, InstancesFuture*& future) {
  Context* context = impl_->GetContext();
  ContextImpl* context_impl = context->GetContextImpl();
  POLARIS_FORK_CHECK()

  ApiStat* api_stat = new ApiStat(context_impl, kApiStatConsumerAsyncGetOne);

  GetOneInstanceRequest::Impl& req_impl = req.GetImpl();
  if (!CheckAndSetRequest(req_impl, __func__, context)) {
    api_stat->Record(kReturnInvalidArgument);
    delete api_stat;
    return kReturnInvalidArgument;
  }

  context_impl->RcuEnter();
  ServiceContext* service_context = context_impl->GetServiceContext(req_impl.service_key_);
  if (service_context == nullptr) {
    api_stat->Record(kReturnInvalidConfig);
    delete api_stat;
    context_impl->RcuExit();
    return kReturnInvalidConfig;
  }
  future = InstancesFuture::Impl::CreateInstancesFuture(api_stat, context_impl, service_context, req_impl);
  context_impl->RcuExit();
  return kReturnOk;
}

ReturnCode ConsumerApi::AsyncGetInstances(const GetInstancesRequest& req, InstancesFuture*& future) {
  Context* context = impl_->GetContext();
  ContextImpl* context_impl = context->GetContextImpl();
  POLARIS_FORK_CHECK()

  ApiStat* api_stat = new ApiStat(context_impl, kApiStatConsumerAsyncGetBatch);

  GetInstancesRequest::Impl& req_impl = req.GetImpl();
  if (!CheckAndSetRequest(req_impl, __func__, context)) {
    api_stat->Record(kReturnInvalidArgument);
    delete api_stat;
    return kReturnInvalidArgument;
  }

  context_impl->RcuEnter();
  ServiceContext* service_context = context_impl->GetServiceContext(req_impl.service_key_);
  if (service_context == nullptr) {
    api_stat->Record(kReturnInvalidConfig);
    delete api_stat;
    context_impl->RcuExit();
    return kReturnInvalidConfig;
  }
  future = InstancesFuture::Impl::CreateInstancesFuture(api_stat, context_impl, service_context, req_impl);
  context_impl->RcuExit();
  return kReturnOk;
}

ReturnCode ConsumerApi::UpdateServiceCallResult(const ServiceCallResult& req) {
  Context* context = impl_->GetContext();
  ContextImpl* context_impl = context->GetContextImpl();
  ApiStat api_stat(context_impl, kApiStatConsumerCallResult);
  ServiceCallResult::Impl& req_impl = req.GetImpl();
  // 检查请求参数
  if (req_impl.gauge_.service_key_.name_.empty()) {
    POLARIS_LOG(LOG_ERROR, "%s failed because request's service name is empty", __func__);
    RECORD_THEN_RETURN(kReturnInvalidArgument);
  }

  POLARIS_FORK_CHECK()

  // 设置Gauge
  InstanceGauge& instance_gauge = req_impl.gauge_;
  ReturnCode ret_code;
  if (instance_gauge.instance_id.empty()) {
    if (req_impl.instance_host_port_ == nullptr) {
      POLARIS_LOG(LOG_ERROR, "%s failed because InstanceId and Host:Port is empty", __func__);
      RECORD_THEN_RETURN(kReturnInvalidArgument);
    }
    // 通过Host:Port获取服务实例ID
    CacheManager* cache_manager = context_impl->GetCacheManager();
    const ServiceKey& service_key = instance_gauge.service_key_;
    ret_code = cache_manager->GetInstanceId(service_key, *req_impl.instance_host_port_, instance_gauge.instance_id);
    if (ret_code != kReturnOk) {
      RECORD_THEN_RETURN(ret_code);
    }
  }
  ret_code = impl_->UpdateServiceCallResult(context, instance_gauge);
  RECORD_THEN_RETURN(ret_code);
}

ReturnCode ConsumerApi::GetRouteRuleKeys(const ServiceKey& service_key, uint64_t timeout,
                                         const std::set<std::string>*& keys) {
  Context* context = impl_->GetContext();
  ContextImpl* context_impl = context->GetContextImpl();
  POLARIS_FORK_CHECK()

  LocalRegistry* local_registry = context->GetLocalRegistry();
  ServiceData* service_data = nullptr;
  context_impl->RcuEnter();
  ReturnCode ret_code = local_registry->GetServiceDataWithRef(service_key, kServiceDataRouteRule, service_data);
  if (ret_code != kReturnOk) {
    ServiceDataNotify* service_notify;
    ret_code =
        local_registry->LoadServiceDataWithNotify(service_key, kServiceDataRouteRule, service_data, service_notify);
    if (ret_code == kReturnOk) {
      timespec ts = Time::SteadyTimeAdd(timeout);
      ret_code = service_notify->WaitDataWithRefUtil(ts, service_data);
    }
  }
  context_impl->RcuExit();
  if (service_data == nullptr) {
    return ret_code;
  }

  if (service_data->GetDataStatus() == kDataNotFound) {
    service_data->DecrementRef();
    return kReturnServiceNotFound;
  }
  ServiceRouteRule route_rule(service_data);
  keys = &route_rule.GetKeys();
  service_data->DecrementRef();
  return kReturnOk;
}

#endif  // ONLY_RATE_LIMIT

ReturnCode ConsumerApiImpl::UpdateServiceCallResult(Context* context, const InstanceGauge& gauge) {
  const ServiceKey& service_key = gauge.service_key_;
  ContextImpl* context_impl = context->GetContextImpl();
  context_impl->RcuEnter();
  ServiceContext* service_context = context_impl->GetServiceContext(service_key);
  if (service_context == nullptr) {
    POLARIS_LOG(LOG_ERROR, "update service call result failed because context of service[%s/%s] not exist",
                service_key.namespace_.c_str(), service_key.name_.c_str());
    context_impl->RcuExit();
    return kReturnInvalidArgument;
  }
  // 执行上报统计插件
  StatReporter* stat_reporter = context_impl->GetStatReporter();
  stat_reporter->ReportStat(gauge);

  // LocalityAwareLoadBalancer Feedback
  if (gauge.locality_aware_info != 0) {
    // locality_aware_info构造时默认为0，LA填写的均非0
    LoadBalancer* load_balancer = service_context->GetLoadBalancer(kLoadBalanceTypeLocalityAware);
    if (load_balancer == nullptr) {
      return kReturnPluginError;
    }
    if (load_balancer != nullptr) {
      LocalityAwareLoadBalancer* locality_aware_load_balancer = dynamic_cast<LocalityAwareLoadBalancer*>(load_balancer);
      if (locality_aware_load_balancer != nullptr) {
        FeedbackInfo info;
        info.call_daley = gauge.call_daley * 1000;  // ms -> us
        info.instance_id = gauge.instance_id;
        info.locality_aware_info = gauge.locality_aware_info;
        locality_aware_load_balancer->Feedback(info);
      }
    }
  }
  // 执行熔断插件
  CircuitBreakerChain* circuit_breaker_chain = service_context->GetCircuitBreakerChain();
  circuit_breaker_chain->RealTimeCircuitBreak(gauge);
  context_impl->RcuExit();
  return kReturnOk;
}

ReturnCode ConsumerApiImpl::GetSystemServer(Context* context, const ServiceKey& service_key, const Criteria& criteria,
                                            Instance*& instance, uint64_t timeout, const std::string& protocol) {
  ContextImpl* context_impl = context->GetContextImpl();
  context_impl->RcuEnter();
  ServiceContext* service_context = context_impl->GetServiceContext(service_key);
  if (service_context == nullptr) {
    context_impl->RcuExit();
    return kReturnInvalidConfig;
  }
  RouteInfo route_info(service_key, nullptr);
  MetadataRouterParam metadata_param;
  metadata_param.metadata_.insert(std::make_pair("protocol", protocol));
  route_info.SetMetadataPara(metadata_param);
  ServiceRouterChain* service_route_chain = service_context->GetServiceRouterChain();
  ReturnCode ret = service_route_chain->PrepareRouteInfo(route_info, timeout);
  if (ret != kReturnOk) {
    context_impl->RcuExit();
    return ret;
  }
  RouteResult route_result;
  if ((ret = service_route_chain->DoRoute(route_info, &route_result)) != kReturnOk) {
    context_impl->RcuExit();
    return ret;
  }
  ServiceInstances* service_instances = route_info.GetServiceInstances();
  LoadBalancer* load_balancer = service_context->GetLoadBalancer(
      criteria.hash_string_.empty() ? kLoadBalanceTypeDefaultConfig : kLoadBalanceTypeRingHash);
  Instance* select_instance = nullptr;
  if ((ret = load_balancer->ChooseInstance(service_instances, criteria, select_instance)) == kReturnOk) {
    instance = select_instance->GetLocalityAwareInfo() == 0 ? new Instance(*select_instance) : select_instance;
  }
  context_impl->RcuExit();
  return ret;
}

void ConsumerApiImpl::UpdateServerResult(Context* context, const ServiceKey& service_key, const Instance& instance,
                                         PolarisServerCode code, CallRetStatus status, uint64_t delay) {
  InstanceGauge instance_gauge;
  instance_gauge.service_key_ = service_key;
  instance_gauge.instance_id = instance.GetId();
  instance_gauge.call_daley = delay;
  instance_gauge.call_ret_code = code;
  instance_gauge.call_ret_status = status;

  ConsumerApiImpl::UpdateServiceCallResult(context, instance_gauge);
  ServerMetric* metric = context->GetContextImpl()->GetServerMetric();
  if (metric != nullptr) {
    ReturnCode ret_code = kReturnOk;
    if (kServerCodeConnectError <= code && code <= kServerCodeInvalidResponse) {
      ret_code = (code == kServerCodeRpcTimeout) ? kReturnTimeout : kReturnServerError;
    }
    metric->MetricReport(service_key, instance, ret_code, status, delay);
  }
}



}  // namespace polaris
