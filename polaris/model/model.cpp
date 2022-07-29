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

#include "polaris/model.h"

#include <google/protobuf/util/json_util.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <v1/response.pb.h>
#include <v1/routing.pb.h>
#include <v1/service.pb.h>

#include <cstring>
#include <iosfwd>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "logger.h"
#include "model/constants.h"
#include "model/instance.h"
#include "model/model_impl.h"
#include "model/service_route_rule.h"
#include "plugin/load_balancer/hash/hash_manager.h"
#include "polaris/defs.h"
#include "polaris/log.h"
#include "polaris/plugin.h"
#include "requests.h"
#include "utils/ip_utils.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

bool operator<(ServiceKey const& lhs, ServiceKey const& rhs) {
  if (lhs.name_.size() < rhs.name_.size()) {
    return true;
  } else if (lhs.name_.size() > rhs.name_.size()) {
    return false;
  }
  int result = std::memcmp(lhs.name_.data(), rhs.name_.data(), lhs.name_.size());
  if (result == 0) {
    return lhs.namespace_ < rhs.namespace_;
  } else {
    return result < 0;
  }
}

bool operator==(const ServiceKey& lhs, const ServiceKey& rhs) {
  return lhs.name_ == rhs.name_ && lhs.namespace_ == rhs.namespace_;
}

///////////////////////////////////////////////////////////////////////////////

ServiceBase::ServiceBase() : ref_count_(1) {}

ServiceBase::~ServiceBase() {
  // 检查引用数必须为0才释放
  POLARIS_ASSERT(ref_count_.load() == 0);
}

void ServiceBase::IncrementRef() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

void ServiceBase::DecrementRef() {
  if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    delete this;
  }
}

uint64_t ServiceBase::DecrementAndGetRef() {
  int before_count = ref_count_.fetch_sub(1, std::memory_order_acq_rel);
  if (before_count == 1) {
    delete this;
  }
  return before_count - 1;
}

///////////////////////////////////////////////////////////////////////////////
InstancesSet::InstancesSet(const std::vector<Instance*>& instances) : impl_(new InstancesSetImpl(instances)) {}

InstancesSet::InstancesSet(const std::vector<Instance*>& instances, const std::map<std::string, std::string>& subset)
    : impl_(new InstancesSetImpl(instances, subset)) {}

InstancesSet::InstancesSet(const std::vector<Instance*>& instances, const std::map<std::string, std::string>& subset,
                           const std::string& recover_info)
    : impl_(new InstancesSetImpl(instances, subset, recover_info)) {}

InstancesSet::~InstancesSet() { delete impl_; }

const std::vector<Instance*>& InstancesSet::GetInstances() const { return impl_->instances_; }

const std::map<std::string, std::string>& InstancesSet::GetSubset() const { return impl_->subset_; }

const std::string& InstancesSet::GetRecoverInfo() const { return impl_->recover_info_; }

void InstancesSet::SetSelector(Selector* selector) { impl_->selector_.reset(selector); }

Selector* InstancesSet::GetSelector() { return impl_->selector_.get(); }

InstancesSetImpl* InstancesSet::GetImpl() const { return impl_; }

bool InstancesSetImpl::UpdateRecoverAll(bool recover_all) {
  bool old_recover_all = recover_all_.load();
  if (old_recover_all == recover_all) {
    return false;
  }
  if (recover_all_.compare_exchange_strong(old_recover_all, recover_all)) {
    return true;
  }
  return false;
}

uint64_t InstancesSetImpl::CalcTotalWeight(const std::vector<Instance*>& instances) {
  uint64_t total_weight = 0;
  for (auto instance : instances) {
    total_weight += instance->GetWeight();
  }
  return total_weight;
}

uint32_t InstancesSetImpl::CalcMaxWeight(const std::vector<Instance*>& instances) {
  uint32_t max_weight = 0;
  for (auto instance : instances) {
    if (instance->GetWeight() > max_weight) {
      max_weight = instance->GetWeight();
    }
  }
  return max_weight;
}

///////////////////////////////////////////////////////////////////////////////
const char* DataTypeToStr(ServiceDataType data_type) {
  switch (data_type) {
    case kServiceDataInstances:
      return "Instances";
    case kServiceDataRouteRule:
      return "RouteRule";
    case kServiceDataRateLimit:
      return "RateLimit";
    case kCircuitBreakerConfig:
      return "CircuitBreakerConfig";
    default:
      return "UnknowType";
  }
}

ServiceInstances::Impl::Impl(ServiceData* service_data)
    : service_data_(service_data),
      available_instances_(nullptr),
      data_(service_data_->GetServiceDataImpl()->data_.instances_),
      dynamic_weight_version_(data_->dynamic_weight_version_.load(std::memory_order_relaxed)) {}

ServiceInstances::ServiceInstances(ServiceData* service_data) : impl_(new Impl(service_data)) {}

ServiceInstances::~ServiceInstances() {}

std::map<std::string, std::string>& ServiceInstances::GetServiceMetadata() { return impl_->data_->metadata_; }

std::map<std::string, Instance*>& ServiceInstances::GetInstances() { return impl_->data_->instances_map_; }

std::set<Instance*>& ServiceInstances::GetUnhealthyInstances() { return impl_->data_->unhealthy_instances_; }

void ServiceInstances::GetHalfOpenInstances(std::set<Instance*>& half_open_instances) {
  std::vector<Instance*> available_instances = GetAvailableInstances()->GetInstances();
  std::map<std::string, int> half_open_instances_map = this->GetService()->GetCircuitBreakerHalfOpenInstances();
  for (std::size_t i = 0; i < available_instances.size(); i++) {
    std::map<std::string, int>::iterator it = half_open_instances_map.find(available_instances[i]->GetId());
    if (it != half_open_instances_map.end()) {
      half_open_instances.insert(available_instances[i]);
    }
  }
}

InstancesSet* ServiceInstances::GetAvailableInstances() {
  if (impl_->available_instances_ != nullptr) {
    return impl_->available_instances_;
  } else {  // 没有设置实例子集，说明全部实例可用
    return impl_->data_->instances_;
  }
}

std::set<Instance*>& ServiceInstances::GetIsolateInstances() { return impl_->data_->isolate_instances_; }

uint64_t ServiceInstances::GetDynamicWeightVersion() const { return impl_->dynamic_weight_version_; }

void ServiceInstances::SetTempDynamicWeightVersion(uint64_t dynamic_weight_version) {
  impl_->dynamic_weight_version_ = dynamic_weight_version;
}

void ServiceInstances::CommitDynamicWeightVersion(uint64_t dynamic_weight_version) {
  impl_->data_->dynamic_weight_version_.store(dynamic_weight_version, std::memory_order_release);
}

void ServiceInstances::UpdateAvailableInstances(InstancesSet* available_instances) {
  impl_->available_instances_ = available_instances;
}

Service* ServiceInstances::GetService() { return impl_->service_data_->GetService(); }

ServiceData* ServiceInstances::GetServiceData() { return impl_->service_data_; }

bool ServiceInstances::IsNearbyEnable() { return impl_->data_->is_enable_nearby_; }

bool ServiceInstances::IsCanaryEnable() { return impl_->data_->is_enable_canary_; }

void ServiceDataImpl::ParseInstancesData(v1::DiscoverResponse& response) {
  data_.instances_ = new InstancesData();
  const ::v1::Service& resp_service = response.service();
  service_key_.namespace_ = resp_service.namespace_().value();
  service_key_.name_ = resp_service.name().value();
  google::protobuf::Map<std::string, std::string>::const_iterator metadata_it;
  static const char kServiceNearbyEnableKey[] = "internal-enable-nearby";
  static const char kServiceCanaryEnableKey[] = "internal-canary";
  data_.instances_->is_enable_nearby_ = false;
  data_.instances_->is_enable_canary_ = false;
  for (metadata_it = resp_service.metadata().begin(); metadata_it != resp_service.metadata().end(); metadata_it++) {
    data_.instances_->metadata_.insert(std::make_pair(metadata_it->first, metadata_it->second));
    if (metadata_it->first == kServiceNearbyEnableKey && StringUtils::IgnoreCaseCmp(metadata_it->second, "true")) {
      data_.instances_->is_enable_nearby_ = true;
    } else if (metadata_it->first == kServiceCanaryEnableKey &&
               StringUtils::IgnoreCaseCmp(metadata_it->second, "true")) {
      data_.instances_->is_enable_canary_ = true;
    }
  }

  Hash64Func hashFunc = nullptr;
  HashManager::Instance().GetHashFunction("murmur3", hashFunc);
  std::map<std::string, Instance*> instanceMap;
  std::map<uint64_t, Instance*> hashMap;
  std::map<uint64_t, Instance*>::iterator it;
  for (int i = 0; i < response.instances().size(); i++) {
    const ::v1::Instance& instance_data = response.instances(i);
    Instance* instance = new Instance();
    InstanceImpl& instance_impl = instance->GetImpl();
    instance_impl.InitFromPb(instance_data);

    uint64_t hashVal =
        hashFunc(static_cast<const void*>(instance_data.id().value().c_str()), instance_data.id().value().size(), 0);
    instance_impl.SetHashValue(hashVal);
    it = hashMap.find(hashVal);
    if (POLARIS_LIKELY(it == hashMap.end())) {
      hashMap[hashVal] = instance;
    } else {
      if (instance->GetPort() == it->second->GetPort() && instance->GetHost() == it->second->GetHost()) {
        POLARIS_LOG(LOG_ERROR, "ns=%s service=%s duplicated instance(%s:%d) id=%s @=%d, skip...",
                    service_key_.namespace_.c_str(), service_key_.name_.c_str(), instance->GetHost().c_str(),
                    instance->GetPort(), instance->GetId().c_str(), i);
        delete instance;
        continue;  // skip duplicated instances
      }
      POLARIS_LOG(LOG_ERROR, "hash conflict. idx=%d %s %s hash=%" PRIu64 "", i, instance->GetId().c_str(),
                  it->second->GetId().c_str(), it->second->GetHash());
      hashVal = HandleHashConflict(hashMap, instance_data, hashFunc);
      if (hashVal != 0) {
        instance_impl.SetHashValue(hashVal);
        hashMap[hashVal] = instance;
      }
    }
    if (instance_data.isolate().value() || instance_data.weight().value() == 0) {
      data_.instances_->isolate_instances_.insert(instance);
      POLARIS_LOG(LOG_TRACE, "service[%s/%s] instance[%s] host[%s] port[%d] %s", service_key_.namespace_.c_str(),
                  service_key_.name_.c_str(), instance_data.id().value().c_str(), instance_data.host().value().c_str(),
                  instance_data.port().value(), instance_data.isolate().value() ? "is isolate" : "weight is 0");
    } else {
      instanceMap[instance->GetId()] = instance;
    }
  }
  std::vector<Instance*> instances;
  for (std::map<std::string, Instance*>::iterator it = instanceMap.begin(); it != instanceMap.end(); ++it) {
    instances.push_back(it->second);
    if (!it->second->isHealthy()) {
      data_.instances_->unhealthy_instances_.insert(it->second);
    }
  }
  data_.instances_->instances_map_.swap(instanceMap);
  revision_ = resp_service.revision().value();
  data_.instances_->instances_ = new InstancesSet(instances);
}

uint64_t ServiceDataImpl::HandleHashConflict(const std::map<uint64_t, Instance*>& hashMap,
                                             const ::v1::Instance& instance_data, Hash64Func hashFunc) {
  int retry = 1;
  char buff[128];
  std::map<uint64_t, Instance*>::const_iterator it;
  while (retry <= 10) {  // 10 次都冲突？我认命, 换个哈希算法吧
    memset(buff, 0, sizeof(buff));
    snprintf(buff, sizeof(buff), "%s:%d", instance_data.id().value().c_str(), retry++);
    uint64_t hashVal = hashFunc(static_cast<const void*>(buff), strlen(buff), 0);
    it = hashMap.find(hashVal);
    if (it != hashMap.end()) {
      POLARIS_LOG(LOG_ERROR, "hash conflict. %s %s hash=%" PRIu64 "", buff, it->second->GetId().c_str(),
                  it->second->GetHash());
    } else {
      POLARIS_LOG(LOG_WARN, "got hash=%" PRIu64 "(%s) after hash conflict for id=%s %s:%d", hashVal, buff,
                  instance_data.id().value().c_str(), instance_data.host().value().c_str(),
                  instance_data.port().value());
      return hashVal;  // got one hash
    }
  }
  POLARIS_LOG(LOG_ERROR, "hash conflict after %d retries. %s %s hash=%" PRIu64 ". try from 1 to uint64_t max", retry,
              buff, it->second->GetId().c_str(), it->second->GetHash());
  uint64_t candidateHash = 0;
  uint64_t maxHash = static_cast<uint64_t>(-1);
  for (candidateHash = 1; candidateHash <= maxHash; ++candidateHash) {  // 全满不可能(内存撑不下)
    if (hashMap.find(candidateHash) == hashMap.end()) {
      POLARIS_LOG(LOG_WARN, "got hash=%" PRIu64 " for %s %s:%d", candidateHash, instance_data.id().value().c_str(),
                  instance_data.host().value().c_str(), instance_data.port().value());
      return candidateHash;  // got one hash
    }
  }
  POLARIS_LOG(LOG_FATAL,
              "Damn it. How can this happen? no value available in [1, uint64_t max]. "
              "DROP it, id:%s %s:%d",
              instance_data.id().value().c_str(), instance_data.host().value().c_str(), instance_data.port().value());
  return 0;
}

static void GetRouteRuleKeys(const v1::Route& route, std::set<std::string>& keys) {
  google::protobuf::Map< ::std::string, ::v1::MatchString>::const_iterator it;
  for (int i = 0; i < route.sources_size(); ++i) {
    const v1::Source& source = route.sources(i);
    for (it = source.metadata().begin(); it != source.metadata().end(); ++it) {
      keys.insert(it->first);
    }
  }
}

void ServiceDataImpl::ParseRouteRuleData(v1::DiscoverResponse& response) {
  const ::v1::Service& service = response.service();
  service_key_.namespace_ = service.namespace_().value();
  service_key_.name_ = service.name().value();
  revision_ = service.revision().value();
  const v1::Routing& routing = response.routing();
  data_.route_rule_ = new RouteRuleData(routing.inbounds_size(), routing.outbounds_size());
  for (int i = 0; i < routing.inbounds_size(); ++i) {
    data_.route_rule_->inbounds_[i].route_rule_.InitFromPb(routing.inbounds(i));
    data_.route_rule_->inbounds_[i].recover_all_ = false;
    GetRouteRuleKeys(routing.inbounds(i), data_.route_rule_->keys_);
  }
  for (int i = 0; i < routing.outbounds_size(); ++i) {
    data_.route_rule_->outbounds_[i].route_rule_.InitFromPb(routing.outbounds(i));
    data_.route_rule_->outbounds_[i].recover_all_ = false;
    GetRouteRuleKeys(routing.outbounds(i), data_.route_rule_->keys_);
  }
}

void ServiceDataImpl::FillSystemVariables(const SystemVariables& variables) {
  std::vector<RouteRuleBound>& inbounds = data_.route_rule_->inbounds_;
  for (std::size_t i = 0; i < inbounds.size(); ++i) {
    inbounds[i].route_rule_.FillSystemVariables(variables);
  }
  std::vector<RouteRuleBound>& outbounds = data_.route_rule_->outbounds_;
  for (std::size_t i = 0; i < outbounds.size(); ++i) {
    outbounds[i].route_rule_.FillSystemVariables(variables);
  }
}

void ServiceDataImpl::ParseRateLimitData(v1::DiscoverResponse& response) {
  const ::v1::Service& service = response.service();
  service_key_.namespace_ = service.namespace_().value();
  service_key_.name_ = service.name().value();
  revision_ = service.revision().value();
  data_.rate_limit_ = new RateLimitData();
  const v1::RateLimit& rate_limit = response.ratelimit();
  int valid_rule_cout = 0;
  for (int i = 0; i < rate_limit.rules_size(); ++i) {
    RateLimitRule* rate_limit_rule = new RateLimitRule();
    const v1::Rule& rule = rate_limit.rules(i);
    if (rate_limit_rule->Init(rule)) {
      data_.rate_limit_->AddRule(rate_limit_rule);
      valid_rule_cout++;
    } else {
      POLARIS_LOG(LOG_INFO, "drop service[%s/%s] rate limit rule: %s", rule.namespace_().value().c_str(),
                  rule.service().value().c_str(), rate_limit.rules(i).id().value().c_str());
      delete rate_limit_rule;
    }
  }
  data_.rate_limit_->SortByPriority();
}

void ServiceDataImpl::ParseCircuitBreaker(v1::DiscoverResponse& response) {
  const ::v1::Service& service = response.service();
  service_key_.namespace_ = service.namespace_().value();
  service_key_.name_ = service.name().value();
  revision_ = service.revision().value();
  data_.circuitBreaker_ = response.release_circuitbreaker();
}

ServiceData::ServiceData(ServiceDataType data_type) {
  impl_ = new ServiceDataImpl();
  impl_->data_type_ = data_type;
  impl_->service_ = nullptr;
}

ServiceData::~ServiceData() {
  if (impl_ != nullptr) {
    if (impl_->data_type_ == kServiceDataInstances) {
      delete impl_->data_.instances_;
    } else if (impl_->data_type_ == kServiceDataRouteRule) {
      delete impl_->data_.route_rule_;
    } else if (impl_->data_type_ == kServiceDataRateLimit) {
      delete impl_->data_.rate_limit_;
    } else if (impl_->data_type_ == kCircuitBreakerConfig) {
      delete impl_->data_.circuitBreaker_;
    }
    delete impl_;
  }
}

ServiceData* ServiceData::CreateFromJson(const std::string& content, ServiceDataStatus data_status,
                                         uint64_t available_time) {
  v1::DiscoverResponse response;
  google::protobuf::util::Status status = google::protobuf::util::JsonStringToMessage(content, &response);
  if (!status.ok()) {
    POLARIS_LOG(LOG_ERROR, "create service data from json[%s] error: %s", content.c_str(), status.ToString().c_str());
    return nullptr;
  }
  ServiceData* service_data = CreateFromPbJson(&response, content, data_status, 0);
  if (service_data != nullptr) {
    service_data->impl_->available_time_ = available_time;
  }
  return service_data;
}

ServiceData* ServiceData::CreateFromPb(void* content, ServiceDataStatus data_status, uint64_t cache_version) {
  // response 由调用者释放
  v1::DiscoverResponse* response = reinterpret_cast<v1::DiscoverResponse*>(content);
  std::string json_content;
  google::protobuf::util::MessageToJsonString(*response, &json_content);
  return CreateFromPbJson(content, json_content, data_status, cache_version);
}

ServiceData* ServiceData::CreateFromPbJson(void* pb_content, const std::string& json_content,
                                           ServiceDataStatus data_status, uint64_t cache_version) {
  // response 由调用者释放
  v1::DiscoverResponse* response = reinterpret_cast<v1::DiscoverResponse*>(pb_content);
  ServiceData* service_data = nullptr;
  if (response->type() == v1::DiscoverResponse::INSTANCE) {
    service_data = new ServiceData(kServiceDataInstances);
    service_data->impl_->ParseInstancesData(*response);
  } else if (response->type() == v1::DiscoverResponse::ROUTING) {
    service_data = new ServiceData(kServiceDataRouteRule);
    service_data->impl_->ParseRouteRuleData(*response);
  } else if (response->type() == v1::DiscoverResponse::RATE_LIMIT) {
    service_data = new ServiceData(kServiceDataRateLimit);
    service_data->impl_->ParseRateLimitData(*response);
  } else if (response->type() == v1::DiscoverResponse::CIRCUIT_BREAKER) {
    service_data = new ServiceData(kCircuitBreakerConfig);
    service_data->impl_->ParseCircuitBreaker(*response);
  } else {
    POLARIS_LOG(LOG_ERROR, "create service data from pb[%s] with error data type",
                response->ShortDebugString().c_str());
    return nullptr;
  }
  service_data->impl_->json_content_ = json_content;
  service_data->impl_->data_status_ = data_status;
  service_data->impl_->cache_version_ = cache_version;
  service_data->impl_->available_time_ = 0;
  return service_data;
}

bool ServiceData::IsAvailable() {
  return impl_->data_status_ == kDataIsSyncing ||
         (impl_->data_status_ == kDataInitFromDisk && Time::GetSystemTimeMs() >= impl_->available_time_);
}

const ServiceKey& ServiceData::GetServiceKey() const { return impl_->service_key_; }

const std::string& ServiceData::GetRevision() const { return impl_->revision_; }

uint64_t ServiceData::GetCacheVersion() { return impl_->cache_version_; }

ServiceDataType ServiceData::GetDataType() { return impl_->data_type_; }

ServiceDataStatus ServiceData::GetDataStatus() { return impl_->data_status_; }

Service* ServiceData::GetService() { return impl_->service_; }

const std::string& ServiceData::ToJsonString() { return impl_->json_content_; }

ServiceDataImpl* ServiceData::GetServiceDataImpl() { return impl_; }

///////////////////////////////////////////////////////////////////////////////
// 服务数据加载通知

DataNotify* ConditionVariableDataNotifyFactory() { return new ConditionVariableDataNotify(); }

DataNotifyFactory g_data_notify_factory = ConditionVariableDataNotifyFactory;

class ConsumerApi;
bool SetDataNotifyFactory(ConsumerApi* consumer, DataNotifyFactory factory) {
  if (consumer == nullptr) {
    POLARIS_LOG(LOG_ERROR, "must create consumer api before set data notify factory");
    return false;
  }
  if (factory != nullptr) {
    g_data_notify_factory = factory;
  } else {
    POLARIS_LOG(LOG_WARN, "set data notify factory to null will reset to default factory");
    g_data_notify_factory = ConditionVariableDataNotifyFactory;
  }
  return true;
}

ServiceDataNotifyImpl::ServiceDataNotifyImpl(const ServiceKey& service_key, ServiceDataType data_type) {
  // 这些服务使用默认的服务数据通知对象
  if (service_key.namespace_ == constants::kPolarisNamespace) {
    data_notify_ = ConditionVariableDataNotifyFactory();
  } else {
    data_notify_ = g_data_notify_factory();
  }
  service_key_ = service_key;
  data_type_ = data_type;
  service_data_ = nullptr;
}

ServiceDataNotifyImpl::~ServiceDataNotifyImpl() {
  if (data_notify_ != nullptr) {
    delete data_notify_;
    data_notify_ = nullptr;
  }
  if (service_data_ != nullptr) {
    service_data_->DecrementRef();
    service_data_ = nullptr;
  }
}

ServiceDataNotify::ServiceDataNotify(const ServiceKey& service_key, ServiceDataType data_type) {
  impl_ = new ServiceDataNotifyImpl(service_key, data_type);
}

ServiceDataNotify::~ServiceDataNotify() {
  if (impl_ != nullptr) {
    delete impl_;
    impl_ = nullptr;
  }
}

bool ServiceDataNotify::hasData() { return impl_->service_data_ != nullptr; }

ReturnCode ServiceDataNotify::WaitDataWithRefUtil(const timespec& ts, ServiceData*& service_data) {
  impl_->service_data_lock_.lock();
  ServiceData* notify_data = impl_->service_data_;
  if (notify_data != nullptr) {  // 已经有值
    notify_data->IncrementRef();
  }
  impl_->service_data_lock_.unlock();
  if (notify_data != nullptr) {     // 已经有值
    if (service_data != nullptr) {  // 磁盘加载的数据
      service_data->DecrementRef();
    }
    service_data = notify_data;
    return kReturnOk;
  }

  if (service_data != nullptr && service_data->IsAvailable()) {
    return kReturnOk;  // 这里直接拿磁盘数据使用
  }

  // 等待加载完成
  uint64_t timeout = Time::SteadyTimeDiff(ts);
  impl_->data_notify_->Wait(timeout);
  impl_->service_data_lock_.lock();
  notify_data = impl_->service_data_;
  if (notify_data != nullptr) {  // 已经有值
    notify_data->IncrementRef();
  }
  impl_->service_data_lock_.unlock();
  if (notify_data != nullptr) {
    if (service_data != nullptr) {  // 磁盘加载的数据
      service_data->DecrementRef();
    }
    service_data = notify_data;
    POLARIS_LOG(LOG_DEBUG, "wait %s data for service[%s/%s] success", DataTypeToStr(impl_->data_type_),
                impl_->service_key_.namespace_.c_str(), impl_->service_key_.name_.c_str());
    return kReturnOk;
  } else if (service_data != nullptr && service_data->GetDataStatus() == kDataInitFromDisk) {
    const ServiceKey& service_key = service_data->GetServiceKey();
    POLARIS_LOG(LOG_WARN, "wait %s data for service[%s/%s] timeout, use service data init from disk",
                DataTypeToStr(impl_->data_type_), service_key.namespace_.c_str(), service_key.name_.c_str());
    return kReturnOk;  // 这里直接拿磁盘数据使用
  } else {
    return kReturnTimeout;
  }
}

void ServiceDataNotify::Notify(ServiceData* service_data) {
  POLARIS_ASSERT(service_data != nullptr);
  POLARIS_ASSERT(service_data->GetServiceKey() == impl_->service_key_);
  POLARIS_ASSERT(service_data->GetDataType() == impl_->data_type_);

  impl_->service_data_lock_.lock();
  if (impl_->service_data_ != nullptr) {
    impl_->service_data_->DecrementRef();
  }
  service_data->IncrementRef();
  impl_->service_data_ = service_data;
  impl_->service_data_lock_.unlock();
  POLARIS_LOG(LOG_DEBUG, "notify %s data for service[%s/%s]", DataTypeToStr(impl_->data_type_),
              impl_->service_key_.namespace_.c_str(), impl_->service_key_.name_.c_str());
  impl_->data_notify_->Notify();
}

///////////////////////////////////////////////////////////////////////////////
ServiceImpl::ServiceImpl(const ServiceKey& service_key, uint32_t service_id)
    : service_key_(service_key),
      service_id_(service_id),
      instance_next_id_(0),
      last_half_open_time_(0),
      try_half_open_count_(0) {
  pthread_rwlock_init(&circuit_breaker_data_lock_, nullptr);
  circuit_breaker_data_version_ = 0;

  have_half_open_data_ = false;

  dynamic_weights_version_ = 0;
  dynamic_weights_data_last_update_time_ = 0;
  dynamic_weights_data_status_ = kDynamicWeightNoInit;
  dynamic_weights_data_sync_interval_ = 0;
  min_dynamic_weight_for_init_ = 0;

  pthread_rwlock_init(&sets_circuit_breaker_data_lock_, nullptr);
  pthread_rwlock_init(&dynamic_weights_data_lock_, nullptr);
  sets_circuit_breaker_data_version_ = 0;
}

ServiceImpl::~ServiceImpl() {
  pthread_rwlock_destroy(&circuit_breaker_data_lock_);
  pthread_rwlock_destroy(&sets_circuit_breaker_data_lock_);
  pthread_rwlock_destroy(&dynamic_weights_data_lock_);
}

void ServiceImpl::UpdateInstanceId(ServiceData* service_data) {
  ServiceInstances service_instances(service_data);
  std::map<std::string, uint64_t> new_instance_id_map;
  std::map<std::string, uint64_t>::iterator id_it;
  uint64_t instance_id_of_service = ((uint64_t)service_id_) << 32;

  std::map<std::string, Instance*>& instances = service_instances.GetInstances();
  std::map<std::string, Instance*>::iterator instance_it;
  for (instance_it = instances.begin(); instance_it != instances.end(); ++instance_it) {
    id_it = instance_id_map_.find(instance_it->second->GetId());
    uint64_t id = id_it != instance_id_map_.end() ? id_it->second : instance_id_of_service | ++instance_next_id_;
    instance_it->second->GetImpl().SetLocalId(id);
    new_instance_id_map[instance_it->second->GetId()] = id;
  }
  std::set<Instance*>& isolates = service_instances.GetIsolateInstances();
  std::set<Instance*>::iterator isolate_it;
  for (isolate_it = isolates.begin(); isolate_it != isolates.end(); ++isolate_it) {
    id_it = instance_id_map_.find((*isolate_it)->GetId());
    uint64_t id = id_it != instance_id_map_.end() ? id_it->second : instance_id_of_service | ++instance_next_id_;
    (*isolate_it)->GetImpl().SetLocalId(id);
    new_instance_id_map[(*isolate_it)->GetId()] = id;
  }

  instance_id_map_.swap(new_instance_id_map);
}

Service::Service(const ServiceKey& service_key, uint32_t service_id) {
  impl_ = new ServiceImpl(service_key, service_id);
}

Service::~Service() {
  if (impl_ != nullptr) {
    delete impl_;
    impl_ = nullptr;
  }
}

ServiceKey& Service::GetServiceKey() { return impl_->service_key_; }

void Service::UpdateData(ServiceData* service_data) {
  if (service_data != nullptr) {
    if (service_data->GetDataType() == kServiceDataInstances) {
      impl_->UpdateInstanceId(service_data);
    }
    service_data->GetServiceDataImpl()->service_ = this;
  }
}

void Service::SetDynamicWeightData(const DynamicWeightData& dynamic_weight_data, bool& states_change) {
  // locklessly make a copy to swap later
  std::map<std::string, uint32_t> old_dynamic_weights = dynamic_weight_data.dynamic_weights;
  uint64_t now_time_ms = Time::GetCoarseSteadyTimeMs();

  // only lightweight swap whitout copying and comparing data, in order to minimize the lock time
  pthread_rwlock_wrlock(&impl_->dynamic_weights_data_lock_);
  impl_->dynamic_weights_data_last_update_time_ = now_time_ms;
  old_dynamic_weights.swap(impl_->dynamic_weights_);  // impl_->dynamic_weights_ is updated now
  if (impl_->dynamic_weights_data_status_ != dynamic_weight_data.status) {
    states_change = true;
  }
  impl_->dynamic_weights_data_status_ = dynamic_weight_data.status;
  impl_->dynamic_weights_data_sync_interval_ = dynamic_weight_data.sync_interval;
  pthread_rwlock_unlock(&impl_->dynamic_weights_data_lock_);

  // calculate dynamic_weights_version_ locklessly
  if (old_dynamic_weights.size() != dynamic_weight_data.dynamic_weights.size()) {
    ++impl_->dynamic_weights_version_;
  } else {
    for (std::map<std::string, uint32_t>::const_iterator it = old_dynamic_weights.begin();
         it != old_dynamic_weights.end(); it++) {
      std::map<std::string, uint32_t>::const_iterator tmp = dynamic_weight_data.dynamic_weights.find(it->first);
      if (tmp == dynamic_weight_data.dynamic_weights.end() || tmp->second != it->second) {
        ++impl_->dynamic_weights_version_;
        break;
      }
    }
  }
}

bool Service::CheckAndSetDynamicWeightExpire() {
  if (impl_->dynamic_weights_data_status_ == kDynamicWeightUpdating &&
      Time::GetCoarseSteadyTimeMs() >
          impl_->dynamic_weights_data_last_update_time_ + 2 * impl_->dynamic_weights_data_sync_interval_) {
    impl_->dynamic_weights_data_status_ = kDynamicWeightInvalid;
    return true;
  }
  return false;
}

uint64_t Service::GetDynamicWeightDataVersion() { return impl_->dynamic_weights_version_; }

std::map<std::string, uint32_t> Service::GetDynamicWeightData() {
  std::map<std::string, uint32_t> result;

  //校验数据合法性，不合法返回空数据, 简单数据可以不加锁
  if (impl_->dynamic_weights_data_status_ == kDynamicWeightUpdating &&
      Time::GetCoarseSteadyTimeMs() <=
          impl_->dynamic_weights_data_last_update_time_ + 2 * impl_->dynamic_weights_data_sync_interval_) {
    pthread_rwlock_rdlock(&impl_->dynamic_weights_data_lock_);
    result = impl_->dynamic_weights_;
    pthread_rwlock_unlock(&impl_->dynamic_weights_data_lock_);
  }

  return result;
}

void Service::SetCircuitBreakerData(const CircuitBreakerData& circuit_breaker_data) {
  if (circuit_breaker_data.version <= impl_->circuit_breaker_data_version_) {
    POLARIS_LOG(LOG_WARN,
                "Skip update circuit breaker data for service[%s/%s] since version[%" PRId64
                "] is less than local registry version[%" PRId64 "]",
                impl_->service_key_.namespace_.c_str(), impl_->service_key_.name_.c_str(), circuit_breaker_data.version,
                impl_->circuit_breaker_data_version_);
    return;
  }
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    for (std::map<std::string, int>::const_iterator it = circuit_breaker_data.half_open_instances.begin();
         it != circuit_breaker_data.half_open_instances.end(); ++it) {
      POLARIS_LOG(LOG_TRACE, "add half open instance:%s with request count:%d", it->first.c_str(), it->second);
    }
    for (std::set<std::string>::const_iterator it = circuit_breaker_data.open_instances.begin();
         it != circuit_breaker_data.open_instances.end(); ++it) {
      POLARIS_LOG(LOG_TRACE, "add open instance:%s", it->c_str());
    }
  }
  pthread_rwlock_wrlock(&impl_->circuit_breaker_data_lock_);
  if (circuit_breaker_data.version > impl_->circuit_breaker_data_version_) {
    impl_->half_open_instances_ = circuit_breaker_data.half_open_instances;
    impl_->open_instances_ = circuit_breaker_data.open_instances;
    impl_->circuit_breaker_data_version_ = circuit_breaker_data.version;
  }
  pthread_rwlock_unlock(&impl_->circuit_breaker_data_lock_);

  // 生成半开优先分配数据
  const std::lock_guard<std::mutex> mutex_guard(impl_->half_open_lock_);  // 加锁
  std::map<std::string, int> half_open_instances = this->GetCircuitBreakerHalfOpenInstances();
  std::map<std::string, int>::iterator half_open_it;
  for (std::map<std::string, int>::iterator it = impl_->half_open_data_.begin(); it != impl_->half_open_data_.end();
       ++it) {
    half_open_it = half_open_instances.find(it->first);
    // 新版本的半开数据中还有该实例，则复制剩余未分配的请求数
    if (half_open_it != half_open_instances.end()) {
      half_open_it->second = it->second;
    }
  }
  impl_->half_open_data_.swap(half_open_instances);
  if (!impl_->half_open_data_.empty()) {
    impl_->have_half_open_data_ = true;
  } else {
    impl_->have_half_open_data_ = false;
    impl_->try_half_open_count_ = 20;
  }
}

uint64_t Service::GetCircuitBreakerDataVersion() { return impl_->circuit_breaker_data_version_; }

std::map<std::string, int> Service::GetCircuitBreakerHalfOpenInstances() {
  std::map<std::string, int> result;
  pthread_rwlock_rdlock(&impl_->circuit_breaker_data_lock_);
  result = impl_->half_open_instances_;
  pthread_rwlock_unlock(&impl_->circuit_breaker_data_lock_);
  return result;
}

std::set<std::string> Service::GetCircuitBreakerOpenInstances() {
  std::set<std::string> result;
  pthread_rwlock_rdlock(&impl_->circuit_breaker_data_lock_);
  result = impl_->open_instances_;
  pthread_rwlock_unlock(&impl_->circuit_breaker_data_lock_);
  return result;
}

ReturnCode Service::TryChooseHalfOpenInstance(std::set<Instance*>& instances, Instance*& instance) {
  if (!impl_->have_half_open_data_ || instances.empty()) {
    return kReturnInstanceNotFound;
  }
  // 控制释放半开节点的评论，有半开节点以后每20个请求
  // 且距离上次释放半开节点超过2s就释放1个半开请求
  if (++impl_->try_half_open_count_ < 20) {
    return kReturnInstanceNotFound;  // 距离上次释放不足20个正常请求
  }
  uint64_t last_half_open_time = impl_->last_half_open_time_.load();
  uint64_t current_time = Time::GetCoarseSteadyTimeMs();
  if (current_time < last_half_open_time + 2000 ||
      !impl_->last_half_open_time_.compare_exchange_strong(last_half_open_time, current_time)) {
    return kReturnInstanceNotFound;  // 距离上一次释放半开间隔不足2s
  }
  impl_->try_half_open_count_ = 0;

  std::set<Instance*>::iterator split_it = instances.begin();
  std::advance(split_it, rand() % instances.size());
  std::set<Instance*>::iterator instance_it;
  std::map<std::string, int>::iterator half_open_data_it;
  const std::lock_guard<std::mutex> mutex_guard(impl_->half_open_lock_);  // 加锁
  if (impl_->have_half_open_data_) {                                      // double check
    for (instance_it = split_it; instance_it != instances.end(); ++instance_it) {
      half_open_data_it = impl_->half_open_data_.find((*instance_it)->GetId());
      if (half_open_data_it != impl_->half_open_data_.end() && half_open_data_it->second > 0) {
        instance = *instance_it;
        half_open_data_it->second--;
        return kReturnOk;
      }
    }
    for (instance_it = instances.begin(); instance_it != split_it; ++instance_it) {
      half_open_data_it = impl_->half_open_data_.find((*instance_it)->GetId());
      if (half_open_data_it != impl_->half_open_data_.end() && half_open_data_it->second > 0) {
        instance = *instance_it;
        half_open_data_it->second--;
        return kReturnOk;
      }
    }
  }
  instance = nullptr;
  return kReturnInstanceNotFound;
}

ReturnCode Service::WriteCircuitBreakerUnhealthySets(const CircuitBreakUnhealthySetsData& unhealthy_sets_data) {
  pthread_rwlock_wrlock(&impl_->sets_circuit_breaker_data_lock_);
  if (unhealthy_sets_data.version <= impl_->sets_circuit_breaker_data_version_) {
    pthread_rwlock_unlock(&impl_->sets_circuit_breaker_data_lock_);
    return kReturnOk;
  }
  impl_->sets_circuit_breaker_data_version_ = unhealthy_sets_data.version;
  impl_->circuit_breaker_unhealthy_sets_ = unhealthy_sets_data.subset_unhealthy_infos;
  pthread_rwlock_unlock(&impl_->sets_circuit_breaker_data_lock_);
  POLARIS_LOG(POLARIS_TRACE, "update set circuit breaker unhealthy set with version:%" PRIu64 " size:%zu",
              unhealthy_sets_data.version, unhealthy_sets_data.subset_unhealthy_infos.size());
  std::map<std::string, SetCircuitBreakerUnhealthyInfo>::const_iterator it;
  for (it = unhealthy_sets_data.subset_unhealthy_infos.begin(); it != unhealthy_sets_data.subset_unhealthy_infos.end();
       ++it) {
    POLARIS_LOG(POLARIS_TRACE, "update set circuit breaker unhealthy judge key:%s status:%d percent:%f",
                it->first.c_str(), it->second.status, it->second.half_open_release_percent);
  }
  return kReturnOk;
}

uint64_t Service::GetCircuitBreakerSetUnhealthyDataVersion() {
  pthread_rwlock_rdlock(&impl_->sets_circuit_breaker_data_lock_);
  uint64_t version = impl_->sets_circuit_breaker_data_version_;
  pthread_rwlock_unlock(&impl_->sets_circuit_breaker_data_lock_);
  return version;
}

std::map<std::string, SetCircuitBreakerUnhealthyInfo> Service::GetCircuitBreakerSetUnhealthySets() {
  std::map<std::string, SetCircuitBreakerUnhealthyInfo> result;
  pthread_rwlock_rdlock(&impl_->sets_circuit_breaker_data_lock_);
  result = impl_->circuit_breaker_unhealthy_sets_;
  pthread_rwlock_unlock(&impl_->sets_circuit_breaker_data_lock_);
  return result;
}

}  // namespace polaris
