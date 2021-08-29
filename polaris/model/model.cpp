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

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/stubs/status.h>
#include <google/protobuf/stubs/stringpiece.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/wrappers.pb.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <v1/circuitbreaker.pb.h>
#include <v1/model.pb.h>
#include <v1/ratelimit.pb.h>
#include <v1/response.pb.h>
#include <v1/routing.pb.h>
#include <v1/service.pb.h>

#include <iosfwd>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "logger.h"
#include "model/constants.h"
#include "model/model_impl.h"
#include "plugin/load_balancer/hash/hash_manager.h"
#include "polaris/accessors.h"
#include "polaris/defs.h"
#include "polaris/log.h"
#include "polaris/plugin.h"
#include "requests.h"
#include "sync/mutex.h"
#include "utils/ip_utils.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

struct timespec;

namespace polaris {
class ConsumerApi;

bool operator<(ServiceKey const& lhs, ServiceKey const& rhs) {
  int result = lhs.name_.compare(rhs.name_);
  if (result < 0) {
    return true;
  } else if (result > 0) {
    return false;
  }
  return lhs.namespace_ < rhs.namespace_;
}

bool operator==(const ServiceKey& lhs, const ServiceKey& rhs) {
  return lhs.name_ == rhs.name_ && lhs.namespace_ == rhs.namespace_;
}

Instance::Instance() { impl = new InstanceImpl(); }

Instance::Instance(const std::string& id, const std::string& host, const int& port,
                   const uint32_t& weight) {
  impl                  = new InstanceImpl();
  impl->id_             = id;
  impl->host_           = host;
  impl->port_           = port;
  impl->weight_         = weight;
  impl->dynamic_weight_ = weight;
  impl->local_id_       = 0;
}

Instance::Instance(const Instance& other) { impl = new InstanceImpl(*other.impl); }

const Instance& Instance::operator=(const Instance& other) {
  if (impl != NULL) {
    *impl = *other.impl;
  } else {
    impl = new InstanceImpl(*other.impl);
  }
  return *this;
}

Instance::~Instance() {
  if (impl != NULL) delete impl;
}

std::string& Instance::GetHost() const { return impl->host_; }

int Instance::GetPort() const { return impl->port_; }

std::string& Instance::GetVpcId() { return impl->vpc_id_; }

std::string& Instance::GetId() { return impl->id_; }

uint64_t Instance::GetLocalId() { return impl->local_id_; }

std::string& Instance::GetProtocol() { return impl->protocol_; }

std::string& Instance::GetVersion() { return impl->version_; }

uint32_t Instance::GetWeight() { return impl->weight_; }

int Instance::GetPriority() { return impl->priority_; }

bool Instance::isHealthy() { return impl->is_healthy_; }

bool Instance::isIsolate() { return impl->is_isolate_; }

std::map<std::string, std::string>& Instance::GetMetadata() { return impl->metadata_; }

std::string& Instance::GetContainerName() { return impl->container_name_; }

std::string& Instance::GetInternalSetName() { return impl->internal_set_name_; }

std::string& Instance::GetLogicSet() { return impl->logic_set_; }

uint32_t Instance::GetDynamicWeight() { return impl->dynamic_weight_; }

std::string& Instance::GetRegion() { return impl->region_; }

std::string& Instance::GetZone() { return impl->zone_; }

std::string& Instance::GetCampus() { return impl->campus_; }

uint64_t Instance::GetHash() { return impl->hash_; }

Instance::InstanceImpl::InstanceImpl()
    : port_(0), weight_(0), local_id_(0), priority_(0), is_healthy_(true), is_isolate_(false),
      hash_(0), dynamic_weight_(100) {
  localValue_ = new InstanceLocalValue();
}

Instance::InstanceImpl::InstanceImpl(const Instance::InstanceImpl& impl) {
  this->localValue_ = NULL;  // 预先赋值为NULL，下面的赋值操作会使用该值做判断
  *this             = impl;
}

const Instance::InstanceImpl& Instance::InstanceImpl::operator=(
    const Instance::InstanceImpl& impl) {
  if (&impl == this) {
    return *this;
  }
  id_     = impl.id_;
  host_   = impl.host_;
  port_   = impl.port_;
  vpc_id_ = impl.vpc_id_;
  weight_ = impl.weight_;

  local_id_                           = impl.local_id_;
  protocol_                           = impl.protocol_;
  version_                            = impl.version_;
  priority_                           = impl.priority_;
  is_healthy_                         = impl.is_healthy_;
  is_isolate_                         = impl.is_isolate_;
  metadata_                           = impl.metadata_;
  logic_set_                          = impl.logic_set_;
  region_                             = impl.region_;
  zone_                               = impl.zone_;
  campus_                             = impl.campus_;
  hash_                               = impl.hash_;
  container_name_                     = impl.container_name_;
  internal_set_name_                  = impl.internal_set_name_;
  InstanceLocalValue* old_local_value = localValue_;
  localValue_                         = impl.localValue_;
  localValue_->IncrementRef();
  if (old_local_value != NULL) {
    old_local_value->DecrementRef();
  }
  dynamic_weight_ = impl.dynamic_weight_;
  return *this;
}

InstanceLocalValue* Instance::GetLocalValue() { return impl->localValue_; }

void InstanceSetter::SetVpcId(const std::string& vpc_id) { instance_.impl->vpc_id_ = vpc_id; }

void InstanceSetter::SetProtocol(const std::string& protocol) {
  instance_.impl->protocol_ = protocol;
}

void InstanceSetter::SetVersion(const std::string& version) { instance_.impl->version_ = version; }

void InstanceSetter::SetPriority(int priority) { instance_.impl->priority_ = priority; }

void InstanceSetter::SetHealthy(bool healthy) { instance_.impl->is_healthy_ = healthy; }

void InstanceSetter::SetIsolate(bool isolate) { instance_.impl->is_isolate_ = isolate; }

void InstanceSetter::SetLogicSet(const std::string& logic_set) {
  instance_.impl->logic_set_ = logic_set;
}

void InstanceSetter::AddMetadataItem(const std::string& key, const std::string& value) {
  instance_.impl->metadata_[key] = value;
  // 解析container_name和internal-set-name
  if (!key.compare(constants::kContainerNameKey)) {
    instance_.impl->container_name_ = value;
  }
  if (!key.compare(constants::kRouterRequestSetNameKey)) {
    instance_.impl->internal_set_name_ = value;
  }
}

void InstanceSetter::SetDynamicWeight(uint32_t dynamic_weight) {
  instance_.impl->dynamic_weight_ = dynamic_weight;
}

void InstanceSetter::SetRegion(const std::string& region) { instance_.impl->region_ = region; }

void InstanceSetter::SetZone(const std::string& zone) { instance_.impl->zone_ = zone; }

void InstanceSetter::SetCampus(const std::string& campus) { instance_.impl->campus_ = campus; }

void InstanceSetter::SetHashValue(uint64_t hashVal) { instance_.impl->hash_ = hashVal; }

void InstanceSetter::SetLocalId(uint64_t local_id) { instance_.impl->local_id_ = local_id; }

void InstanceSetter::SetLocalValue(InstanceLocalValue* localValue) {
  instance_.impl->localValue_ = localValue;
}

void InstanceSetter::CopyLocalValue(const InstanceSetter& setter) {
  InstanceLocalValue* val = setter.instance_.impl->localValue_;
  POLARIS_ASSERT(val != NULL);
  val->IncrementRef();

  InstanceLocalValue* oldVal  = instance_.impl->localValue_;
  instance_.impl->localValue_ = val;
  POLARIS_ASSERT(oldVal != NULL);
  oldVal->DecrementRef();
}

///////////////////////////////////////////////////////////////////////////////
ServiceBase::ServiceBase() {
  impl_             = new ServiceBaseImpl();
  impl_->ref_count_ = 1;
}

ServiceBase::~ServiceBase() {
  if (impl_ != NULL) {
    // 检查引用数必须为0才释放
    POLARIS_ASSERT(impl_->ref_count_ == 0);
    delete impl_;
  }
}

void ServiceBase::IncrementRef() { ATOMIC_INC(&impl_->ref_count_); }

void ServiceBase::DecrementRef() {
  int pre_count = ATOMIC_DEC(&impl_->ref_count_);
  if (pre_count == 1) {
    delete this;
  }
}

uint64_t ServiceBase::DecrementAndGetRef() {
  int after_count = ATOMIC_DEC_THEN_GET(&impl_->ref_count_);
  if (after_count == 0) {
    delete this;
  }
  return after_count;
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

ServiceInstances::ServiceInstances(ServiceData* service_data) {
  impl_                           = new ServiceInstancesImpl();
  impl_->service_data_            = service_data;
  impl_->data_                    = impl_->service_data_->GetServiceDataImpl()->data_.instances_;
  impl_->all_instances_available_ = true;
  impl_->available_instances_     = NULL;
}

ServiceInstances::~ServiceInstances() {
  if (impl_ != NULL) {
    impl_->data_ = NULL;
    if (impl_->available_instances_ != NULL) {
      impl_->available_instances_->DecrementRef();
      impl_->available_instances_ = NULL;
    }
    if (impl_->service_data_ != NULL) {
      impl_->service_data_->DecrementRef();
      impl_->service_data_ = NULL;
    }
    delete impl_;
    impl_ = NULL;
  }
}

std::map<std::string, std::string>& ServiceInstances::GetServiceMetadata() {
  return impl_->data_->metadata_;
}

std::map<std::string, Instance*>& ServiceInstances::GetInstances() {
  return impl_->data_->instances_map_;
}

std::set<Instance*>& ServiceInstances::GetUnhealthyInstances() {
  return impl_->data_->unhealthy_instances_;
}

void ServiceInstances::GetHalfOpenInstances(std::set<Instance*>& half_open_instances) {
  std::vector<Instance*> available_instances = GetAvailableInstances()->GetInstances();
  std::map<std::string, int> half_open_instances_map =
      this->GetService()->GetCircuitBreakerHalfOpenInstances();
  for (std::size_t i = 0; i < available_instances.size(); i++) {
    std::map<std::string, int>::iterator it =
        half_open_instances_map.find(available_instances[i]->GetId());
    if (it != half_open_instances_map.end()) {
      half_open_instances.insert(available_instances[i]);
    }
  }
}

InstancesSet* ServiceInstances::GetAvailableInstances() {
  if (impl_->all_instances_available_) {
    return impl_->data_->instances_;
  } else {
    return impl_->available_instances_;
  }
}

std::set<Instance*>& ServiceInstances::GetIsolateInstances() {
  return impl_->data_->isolate_instances_;
}

void ServiceInstances::UpdateAvailableInstances(InstancesSet* available_instances) {
  impl_->all_instances_available_ = false;
  if (impl_->available_instances_ != NULL) {
    impl_->available_instances_->DecrementRef();
  }
  available_instances->IncrementRef();
  impl_->available_instances_ = available_instances;
}

Service* ServiceInstances::GetService() { return impl_->service_data_->GetService(); }

ServiceData* ServiceInstances::GetServiceData() { return impl_->service_data_; }

bool ServiceInstances::IsNearbyEnable() { return impl_->data_->is_enable_nearby_; }

bool ServiceInstances::IsCanaryEnable() { return impl_->data_->is_enable_canary_; }

ServiceRouteRule::ServiceRouteRule(ServiceData* service_data) { service_data_ = service_data; }

ServiceRouteRule::~ServiceRouteRule() {
  if (service_data_ != NULL) {
    service_data_->DecrementRef();
    service_data_ = NULL;
  }
}

void* ServiceRouteRule::RouteRule() {
  return service_data_->GetServiceDataImpl()->data_.route_rule_;
}

const std::set<std::string>& ServiceRouteRule::GetKeys() const {
  return service_data_->GetServiceDataImpl()->data_.route_rule_->keys_;
}

ServiceData* ServiceRouteRule::GetServiceData() { return service_data_; }

void ServiceDataImpl::ParseInstancesData(v1::DiscoverResponse& response) {
  data_.instances_                  = new InstancesData();
  const ::v1::Service& resp_service = response.service();
  service_key_.namespace_           = resp_service.namespace_().value();
  service_key_.name_                = resp_service.name().value();
  google::protobuf::Map<std::string, std::string>::const_iterator metadata_it;
  static const char kServiceNearbyEnableKey[] = "internal-enable-nearby";
  static const char kServiceCanaryEnableKey[] = "internal-canary";
  data_.instances_->is_enable_nearby_         = false;
  data_.instances_->is_enable_canary_         = false;
  for (metadata_it = resp_service.metadata().begin(); metadata_it != resp_service.metadata().end();
       metadata_it++) {
    data_.instances_->metadata_.insert(std::make_pair(metadata_it->first, metadata_it->second));
    if (metadata_it->first == kServiceNearbyEnableKey &&
        StringUtils::IgnoreCaseCmp(metadata_it->second, "true")) {
      data_.instances_->is_enable_nearby_ = true;
    } else if (metadata_it->first == kServiceCanaryEnableKey &&
               StringUtils::IgnoreCaseCmp(metadata_it->second, "true")) {
      data_.instances_->is_enable_canary_ = true;
    }
  }

  Hash64Func hashFunc = NULL;
  HashManager::Instance().GetHashFunction("murmur3", hashFunc);
  std::map<std::string, Instance*> instanceMap;
  std::map<uint64_t, Instance*> hashMap;
  std::map<uint64_t, Instance*>::iterator it;
  for (int i = 0; i < response.instances().size(); i++) {
    const ::v1::Instance& instance_data = response.instances(i);
    Instance* instance = new Instance(instance_data.id().value(), instance_data.host().value(),
                                      instance_data.port().value(), instance_data.weight().value());
    InstanceSetter instance_setter(*instance);
    instance_setter.SetVpcId(instance_data.vpc_id().value());
    instance_setter.SetProtocol(instance_data.protocol().value());
    instance_setter.SetVersion(instance_data.version().value());
    instance_setter.SetPriority(instance_data.priority().value());
    if (instance_data.has_healthy()) {
      instance_setter.SetHealthy(instance_data.healthy().value());
    }
    if (instance_data.has_isolate()) {
      instance_setter.SetIsolate(instance_data.isolate().value());
    }
    for (metadata_it = instance_data.metadata().begin();
         metadata_it != instance_data.metadata().end(); metadata_it++) {
      instance_setter.AddMetadataItem(metadata_it->first, metadata_it->second);
    }
    instance_setter.SetLogicSet(instance_data.logic_set().value());
    if (instance_data.has_location()) {
      instance_setter.SetRegion(instance_data.location().region().value());
      instance_setter.SetZone(instance_data.location().zone().value());
      instance_setter.SetCampus(instance_data.location().campus().value());
    }
    // 设置动态权重大小默认为静态权重
    instance_setter.SetDynamicWeight(instance_data.weight().value());
    uint64_t hashVal = hashFunc(static_cast<const void*>(instance_data.id().value().c_str()),
                                instance_data.id().value().size(), 0);
    instance_setter.SetHashValue(hashVal);
    it = hashMap.find(hashVal);
    if (POLARIS_LIKELY(it == hashMap.end())) {
      hashMap[hashVal] = instance;
    } else {
      if (instance->GetPort() == it->second->GetPort() &&
          0 == instance->GetHost().compare(it->second->GetHost())) {
        POLARIS_LOG(LOG_ERROR, "ns=%s service=%s duplicated instance(%s:%d) id=%s @=%d, skip...",
                    service_key_.namespace_.c_str(), service_key_.name_.c_str(),
                    instance->GetHost().c_str(), instance->GetPort(), instance->GetId().c_str(), i);
        continue;  // skip duplicated instances
      }
      POLARIS_LOG(LOG_ERROR, "hash conflict. idx=%d %s %s hash=%" PRIu64 "", i,
                  instance->GetId().c_str(), it->second->GetId().c_str(), it->second->GetHash());
      hashVal = HandleHashConflict(hashMap, instance_data, hashFunc);
      if (hashVal != 0) {
        instance_setter.SetHashValue(hashVal);
        hashMap[hashVal] = instance;
      }
    }
    if (instance_data.isolate().value() || instance_data.weight().value() == 0) {
      data_.instances_->isolate_instances_.insert(instance);
      POLARIS_LOG(LOG_TRACE, "service[%s/%s] instance[%s] host[%s] port[%d] %s",
                  service_key_.namespace_.c_str(), service_key_.name_.c_str(),
                  instance_data.id().value().c_str(), instance_data.host().value().c_str(),
                  instance_data.port().value(),
                  instance_data.isolate().value() ? "is isolate" : "weight is 0");
    } else {
      instanceMap[instance->GetId()] = instance;
    }
  }
  std::vector<Instance*> instances;
  for (std::map<std::string, Instance*>::iterator it = instanceMap.begin(); it != instanceMap.end();
       ++it) {
    instances.push_back(it->second);
    if (!it->second->isHealthy()) {
      data_.instances_->unhealthy_instances_.insert(it->second);
    }
  }
  data_.instances_->instances_map_.swap(instanceMap);
  revision_                    = resp_service.revision().value();
  data_.instances_->instances_ = new InstancesSet(instances);
}

uint64_t ServiceDataImpl::HandleHashConflict(const std::map<uint64_t, Instance*>& hashMap,
                                             const ::v1::Instance& instance_data,
                                             Hash64Func hashFunc) {
  int retry = 1;
  char buff[128];
  std::map<uint64_t, Instance*>::const_iterator it;
  while (retry <= 10) {  // 10 次都冲突？我认命, 换个哈希算法吧
    memset(buff, 0, sizeof(buff));
    snprintf(buff, sizeof(buff), "%s:%d", instance_data.id().value().c_str(), retry++);
    uint64_t hashVal = hashFunc(static_cast<const void*>(buff), strlen(buff), 0);
    it               = hashMap.find(hashVal);
    if (it != hashMap.end()) {
      POLARIS_LOG(LOG_ERROR, "hash conflict. %s %s hash=%" PRIu64 "", buff,
                  it->second->GetId().c_str(), it->second->GetHash());
    } else {
      POLARIS_LOG(LOG_WARN, "got hash=%" PRIu64 "(%s) after hash conflict for id=%s %s:%d", hashVal,
                  buff, instance_data.id().value().c_str(), instance_data.host().value().c_str(),
                  instance_data.port().value());
      return hashVal;  // got one hash
    }
  }
  POLARIS_LOG(LOG_ERROR,
              "hash conflict after %d retries. %s %s hash=%" PRIu64 ". try from 1 to uint64_t max",
              retry, buff, it->second->GetId().c_str(), it->second->GetHash());
  uint64_t candidateHash = 0;
  uint64_t maxHash       = static_cast<uint64_t>(-1);
  for (candidateHash = 1; candidateHash <= maxHash; ++candidateHash) {  // 全满不可能(内存撑不下)
    if (hashMap.find(candidateHash) == hashMap.end()) {
      POLARIS_LOG(LOG_WARN, "got hash=%" PRIu64 " for %s %s:%d", candidateHash,
                  instance_data.id().value().c_str(), instance_data.host().value().c_str(),
                  instance_data.port().value());
      return candidateHash;  // got one hash
    }
  }
  POLARIS_LOG(LOG_FATAL,
              "Damn it. How can this happen? no value available in [1, uint64_t max]. "
              "DROP it, id:%s %s:%d",
              instance_data.id().value().c_str(), instance_data.host().value().c_str(),
              instance_data.port().value());
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
  service_key_.namespace_      = service.namespace_().value();
  service_key_.name_           = service.name().value();
  revision_                    = service.revision().value();
  data_.route_rule_            = new RouteRuleData();
  const v1::Routing& routing   = response.routing();
  data_.route_rule_->inbounds_.resize(routing.inbounds_size());
  for (int i = 0; i < routing.inbounds_size(); ++i) {
    data_.route_rule_->inbounds_[i].route_rule_.InitFromPb(routing.inbounds(i));
    data_.route_rule_->inbounds_[i].recover_all_ = false;
    GetRouteRuleKeys(routing.inbounds(i), data_.route_rule_->keys_);
  }
  data_.route_rule_->outbounds_.resize(routing.outbounds_size());
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
  const ::v1::Service& service    = response.service();
  service_key_.namespace_         = service.namespace_().value();
  service_key_.name_              = service.name().value();
  revision_                       = service.revision().value();
  data_.rate_limit_               = new RateLimitData();
  const v1::RateLimit& rate_limit = response.ratelimit();
  int valid_rule_cout             = 0;
  for (int i = 0; i < rate_limit.rules_size(); ++i) {
    RateLimitRule* rate_limit_rule = new RateLimitRule();
    const v1::Rule& rule           = rate_limit.rules(i);
    if (rate_limit_rule->Init(rule)) {
      data_.rate_limit_->AddRule(rate_limit_rule);
      valid_rule_cout++;
    } else {
      POLARIS_LOG(LOG_INFO, "drop service[%s/%s] rate limit rule: %s",
                  rule.namespace_().value().c_str(), rule.service().value().c_str(),
                  rate_limit.rules(i).id().value().c_str());
      delete rate_limit_rule;
    }
  }
  data_.rate_limit_->SortByPriority();
  if (valid_rule_cout > 20) {
    data_.rate_limit_->SetupIndexMap();
  }
}

void ServiceDataImpl::ParseCircuitBreaker(v1::DiscoverResponse& response) {
  const ::v1::Service& service = response.service();
  service_key_.namespace_      = service.namespace_().value();
  service_key_.name_           = service.name().value();
  revision_                    = service.revision().value();
  data_.circuitBreaker_        = response.release_circuitbreaker();
}

ServiceData::ServiceData(ServiceDataType data_type) {
  impl_             = new ServiceDataImpl();
  impl_->data_type_ = data_type;
  impl_->service_   = NULL;
}

ServiceData::~ServiceData() {
  if (impl_ != NULL) {
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
  google::protobuf::util::Status status =
      google::protobuf::util::JsonStringToMessage(content, &response);
  if (!status.ok()) {
    POLARIS_LOG(LOG_ERROR, "create service data from json[%s] error: %s", content.c_str(),
                status.error_message().data());
    return NULL;
  }
  ServiceData* service_data = CreateFromPbJson(&response, content, data_status, 0);
  if (service_data != NULL) {
    service_data->impl_->available_time_ = available_time;
  }
  return service_data;
}

ServiceData* ServiceData::CreateFromPb(void* content, ServiceDataStatus data_status,
                                       uint64_t cache_version) {
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
  ServiceData* service_data      = NULL;
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
    return NULL;
  }
  service_data->impl_->json_content_   = json_content;
  service_data->impl_->data_status_    = data_status;
  service_data->impl_->cache_version_  = cache_version;
  service_data->impl_->available_time_ = 0;
  return service_data;
}

bool ServiceData::IsAvailable() { return Time::GetCurrentTimeMs() >= impl_->available_time_; }

ServiceKey& ServiceData::GetServiceKey() { return impl_->service_key_; }

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

bool SetDataNotifyFactory(ConsumerApi* consumer, DataNotifyFactory factory) {
  if (consumer == NULL) {
    POLARIS_LOG(LOG_ERROR, "must create consumer api before set data notify factory");
    return false;
  }
  if (factory != NULL) {
    g_data_notify_factory = factory;
  } else {
    POLARIS_LOG(LOG_WARN, "set data notify factory to null will reset to default factory");
    g_data_notify_factory = ConditionVariableDataNotifyFactory;
  }
  return true;
}

ServiceDataNotifyImpl::ServiceDataNotifyImpl(const ServiceKey& service_key,
                                             ServiceDataType data_type) {
  // 这些服务使用默认的服务数据通知对象
  if (service_key.namespace_ == constants::kPolarisNamespace) {
    data_notify_ = ConditionVariableDataNotifyFactory();
  } else {
    data_notify_ = g_data_notify_factory();
  }
  service_key_  = service_key;
  data_type_    = data_type;
  service_data_ = NULL;
}

ServiceDataNotifyImpl::~ServiceDataNotifyImpl() {
  if (data_notify_ != NULL) {
    delete data_notify_;
    data_notify_ = NULL;
  }
  if (service_data_ != NULL) {
    service_data_->DecrementRef();
    service_data_ = NULL;
  }
}

ServiceDataNotify::ServiceDataNotify(const ServiceKey& service_key, ServiceDataType data_type) {
  impl_ = new ServiceDataNotifyImpl(service_key, data_type);
}

ServiceDataNotify::~ServiceDataNotify() {
  if (impl_ != NULL) {
    delete impl_;
    impl_ = NULL;
  }
}

bool ServiceDataNotify::hasData() { return impl_->service_data_ != NULL; }

ReturnCode ServiceDataNotify::WaitDataWithRefUtil(const timespec& ts, ServiceData*& service_data) {
  impl_->service_data_lock_.Lock();
  ServiceData* notify_data = impl_->service_data_;
  if (notify_data != NULL) {  // 已经有值
    notify_data->IncrementRef();
  }
  impl_->service_data_lock_.Unlock();
  if (notify_data != NULL) {     // 已经有值
    if (service_data != NULL) {  // 磁盘加载的数据
      service_data->DecrementRef();
    }
    service_data = notify_data;
    return kReturnOk;
  }

  if (service_data != NULL && service_data->GetDataStatus() == kDataInitFromDisk &&
      service_data->IsAvailable()) {
    return kReturnOk;  // 这里直接拿磁盘数据使用
  }

  // 等待加载完成
  uint64_t timeout = Time::DiffMsWithCurrentTime(ts);
  impl_->data_notify_->Wait(timeout);
  impl_->service_data_lock_.Lock();
  notify_data = impl_->service_data_;
  if (notify_data != NULL) {  // 已经有值
    notify_data->IncrementRef();
  }
  impl_->service_data_lock_.Unlock();
  if (notify_data != NULL) {
    if (service_data != NULL) {  // 磁盘加载的数据
      service_data->DecrementRef();
    }
    service_data = notify_data;
    POLARIS_LOG(LOG_DEBUG, "wait %s data for service[%s/%s] success",
                DataTypeToStr(impl_->data_type_), impl_->service_key_.namespace_.c_str(),
                impl_->service_key_.name_.c_str());
    return kReturnOk;
  } else if (service_data != NULL && service_data->GetDataStatus() == kDataInitFromDisk) {
    ServiceKey& service_key = service_data->GetServiceKey();
    POLARIS_LOG(LOG_WARN,
                "wait %s data for service[%s/%s] timeout, use service data init from disk",
                DataTypeToStr(impl_->data_type_), service_key.namespace_.c_str(),
                service_key.name_.c_str());
    return kReturnOk;  // 这里直接拿磁盘数据使用
  } else {
    return kReturnTimeout;
  }
}

void ServiceDataNotify::Notify(ServiceData* service_data) {
  POLARIS_ASSERT(service_data != NULL);
  POLARIS_ASSERT(service_data->GetServiceKey() == impl_->service_key_);
  POLARIS_ASSERT(service_data->GetDataType() == impl_->data_type_);

  impl_->service_data_lock_.Lock();
  if (impl_->service_data_ != NULL) {
    impl_->service_data_->DecrementRef();
  }
  service_data->IncrementRef();
  impl_->service_data_ = service_data;
  impl_->service_data_lock_.Unlock();
  POLARIS_LOG(LOG_DEBUG, "notify %s data for service[%s/%s]", DataTypeToStr(impl_->data_type_),
              impl_->service_key_.namespace_.c_str(), impl_->service_key_.name_.c_str());
  impl_->data_notify_->Notify();
}

///////////////////////////////////////////////////////////////////////////////
ServiceImpl::ServiceImpl(const ServiceKey& service_key, uint32_t service_id)
    : service_key_(service_key), service_id_(service_id), instance_next_id_(0) {
  pthread_rwlock_init(&circuit_breaker_data_lock_, NULL);
  circuit_breaker_data_version_ = 0;

  have_half_open_data_ = false;

  dynamic_weights_version_     = 0;
  min_dynamic_weight_for_init_ = 0;

  pthread_rwlock_init(&sets_circuit_breaker_data_lock_, NULL);
  sets_circuit_breaker_data_version_ = 0;
}

ServiceImpl::~ServiceImpl() {
  pthread_rwlock_destroy(&circuit_breaker_data_lock_);
  pthread_rwlock_destroy(&sets_circuit_breaker_data_lock_);
}

void ServiceImpl::UpdateInstanceId(ServiceData* service_data) {
  service_data->IncrementRef();
  ServiceInstances service_instances(service_data);
  std::map<std::string, uint64_t> new_instance_id_map;
  std::map<std::string, uint64_t>::iterator id_it;
  uint64_t instance_id_of_service = ((uint64_t)service_id_) << 32;

  std::map<std::string, Instance*>& instances = service_instances.GetInstances();
  std::map<std::string, Instance*>::iterator instance_it;
  for (instance_it = instances.begin(); instance_it != instances.end(); ++instance_it) {
    id_it       = instance_id_map_.find(instance_it->second->GetId());
    uint64_t id = id_it != instance_id_map_.end() ? id_it->second
                                                  : instance_id_of_service | ++instance_next_id_;
    InstanceSetter setter(*instance_it->second);
    setter.SetLocalId(id);
    new_instance_id_map[instance_it->second->GetId()] = id;
  }
  std::set<Instance*>& isolates = service_instances.GetIsolateInstances();
  std::set<Instance*>::iterator isolate_it;
  for (isolate_it = isolates.begin(); isolate_it != isolates.end(); ++isolate_it) {
    id_it       = instance_id_map_.find((*isolate_it)->GetId());
    uint64_t id = id_it != instance_id_map_.end() ? id_it->second
                                                  : instance_id_of_service | ++instance_next_id_;
    InstanceSetter setter(**isolate_it);
    setter.SetLocalId(id);
    new_instance_id_map[(*isolate_it)->GetId()] = id;
  }

  instance_id_map_.swap(new_instance_id_map);
}

Service::Service(const ServiceKey& service_key, uint32_t service_id) {
  impl_ = new ServiceImpl(service_key, service_id);
}

Service::~Service() {
  if (impl_ != NULL) {
    delete impl_;
    impl_ = NULL;
  }
}

ServiceKey& Service::GetServiceKey() { return impl_->service_key_; }

void Service::UpdateData(ServiceData* service_data) {
  if (service_data != NULL) {
    if (service_data->GetDataType() == kServiceDataInstances) {
      impl_->UpdateInstanceId(service_data);
    }
    service_data->GetServiceDataImpl()->service_ = this;
  }
}

void Service::SetDynamicWeightData(const DynamicWeightData& /*dynamic_weight_data*/) {
  // TODO
}

uint64_t Service::GetDynamicWeightDataVersion() { return impl_->dynamic_weights_version_; }

std::map<std::string, uint32_t> Service::GetDynamicWeightData() { return impl_->dynamic_weights_; }

void Service::SetCircuitBreakerData(const CircuitBreakerData& circuit_breaker_data) {
  if (circuit_breaker_data.version <= impl_->circuit_breaker_data_version_) {
    POLARIS_LOG(LOG_WARN,
                "Skip update circuit breaker data for service[%s/%s] since version[%" PRId64
                "] is less than local registry version[%" PRId64 "]",
                impl_->service_key_.namespace_.c_str(), impl_->service_key_.name_.c_str(),
                circuit_breaker_data.version, impl_->circuit_breaker_data_version_);
    return;
  }
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    for (std::map<std::string, int>::const_iterator it =
             circuit_breaker_data.half_open_instances.begin();
         it != circuit_breaker_data.half_open_instances.end(); ++it) {
      POLARIS_LOG(LOG_TRACE, "add half open instance:%s with request count:%d", it->first.c_str(),
                  it->second);
    }
    for (std::set<std::string>::const_iterator it = circuit_breaker_data.open_instances.begin();
         it != circuit_breaker_data.open_instances.end(); ++it) {
      POLARIS_LOG(LOG_TRACE, "add open instance:%s", it->c_str());
    }
  }
  pthread_rwlock_wrlock(&impl_->circuit_breaker_data_lock_);
  if (circuit_breaker_data.version > impl_->circuit_breaker_data_version_) {
    impl_->half_open_instances_          = circuit_breaker_data.half_open_instances;
    impl_->open_instances_               = circuit_breaker_data.open_instances;
    impl_->circuit_breaker_data_version_ = circuit_breaker_data.version;
  }
  pthread_rwlock_unlock(&impl_->circuit_breaker_data_lock_);

  // 生成半开优先分配数据
  sync::MutexGuard mutex_guard(impl_->half_open_lock_);  // 加锁
  std::map<std::string, int> half_open_instances = this->GetCircuitBreakerHalfOpenInstances();
  std::map<std::string, int>::iterator half_open_it;
  for (std::map<std::string, int>::iterator it = impl_->half_open_data_.begin();
       it != impl_->half_open_data_.end(); ++it) {
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
  std::set<Instance*>::iterator split_it = instances.begin();
  std::advance(split_it, rand() % instances.size());
  std::set<Instance*>::iterator instance_it;
  std::map<std::string, int>::iterator half_open_data_it;
  sync::MutexGuard mutex_guard(impl_->half_open_lock_);  // 加锁
  if (impl_->have_half_open_data_) {                     // double check
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
  instance = NULL;
  return kReturnInstanceNotFound;
}

ReturnCode Service::WriteCircuitBreakerUnhealthySets(
    const CircuitBreakUnhealthySetsData& unhealthy_sets_data) {
  pthread_rwlock_wrlock(&impl_->sets_circuit_breaker_data_lock_);
  if (unhealthy_sets_data.version <= impl_->sets_circuit_breaker_data_version_) {
    pthread_rwlock_unlock(&impl_->sets_circuit_breaker_data_lock_);
    return kReturnOk;
  }
  impl_->sets_circuit_breaker_data_version_ = unhealthy_sets_data.version;
  impl_->circuit_breaker_unhealthy_sets_    = unhealthy_sets_data.subset_unhealthy_infos;
  pthread_rwlock_unlock(&impl_->sets_circuit_breaker_data_lock_);
  POLARIS_LOG(POLARIS_TRACE,
              "update set circuit breaker unhealthy set with version:%" PRIu64 " size:%zu",
              unhealthy_sets_data.version, unhealthy_sets_data.subset_unhealthy_infos.size());
  std::map<std::string, SetCircuitBreakerUnhealthyInfo>::const_iterator it;
  for (it = unhealthy_sets_data.subset_unhealthy_infos.begin();
       it != unhealthy_sets_data.subset_unhealthy_infos.end(); ++it) {
    POLARIS_LOG(POLARIS_TRACE,
                "update set circuit breaker unhealthy judge key:%s status:%d percent:%f",
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

///////////////////////////////////////////////////////////////////////////////
// 服务路由相关数据结构

RouteInfo::RouteInfo(const ServiceKey& service_key, ServiceInfo* source_service_info) {
  service_key_               = service_key;
  source_service_info_       = source_service_info;
  service_instances_         = NULL;
  service_route_rule_        = NULL;
  source_service_route_rule_ = NULL;
  route_flag_                = 0;
  disable_routers_           = NULL;
  end_route_                 = false;
  labels_                    = NULL;
  metadata_param_            = NULL;
}

RouteInfo::~RouteInfo() {
  if (source_service_info_ != NULL) {
    delete source_service_info_;
    source_service_info_ = NULL;
  }
  if (service_instances_ != NULL) {
    delete service_instances_;
    service_instances_ = NULL;
  }
  if (service_route_rule_ != NULL) {
    delete service_route_rule_;
    service_route_rule_ = NULL;
  }
  if (source_service_route_rule_ != NULL) {
    delete source_service_route_rule_;
    source_service_route_rule_ = NULL;
  }
  if (disable_routers_ != NULL) {
    delete disable_routers_;
    disable_routers_ = NULL;
  }
  if (labels_ != NULL) {
    delete labels_;
    labels_ = NULL;
  }
  if (metadata_param_ != NULL) {
    delete metadata_param_;
    metadata_param_ = NULL;
  }
}

const ServiceKey& RouteInfo::GetServiceKey() { return service_key_; }

ServiceInfo* RouteInfo::GetSourceServiceInfo() { return source_service_info_; }

ServiceInstances* RouteInfo::GetServiceInstances() { return service_instances_; }

ServiceRouteRule* RouteInfo::GetServiceRouteRule() { return service_route_rule_; }

ServiceRouteRule* RouteInfo::GetSourceServiceRouteRule() { return source_service_route_rule_; }

void RouteInfo::SetServiceInstances(ServiceInstances* service_instances) {
  service_instances_ = service_instances;
}

void RouteInfo::SetServiceRouteRule(ServiceRouteRule* service_route_rule) {
  service_route_rule_ = service_route_rule;
}

void RouteInfo::SetSourceServiceRouteRule(ServiceRouteRule* source_service_route_rule) {
  source_service_route_rule_ = source_service_route_rule;
}

void RouteInfo::UpdateServiceInstances(ServiceInstances* service_instances) {
  if (service_instances_ != service_instances) {
    delete service_instances_;
    service_instances_ = service_instances;
  }
}

void RouteInfo::SetIncludeUnhealthyInstances() {
  route_flag_ = route_flag_ | kGetInstancesRequestIncludeUnhealthy;
}

void RouteInfo::SetIncludeCircuitBreakerInstances() {
  route_flag_ = route_flag_ | kGetInstancesRequestIncludeCircuitBreaker;
}

bool RouteInfo::IsIncludeUnhealthyInstances() {
  return route_flag_ & kGetInstancesRequestIncludeUnhealthy;
}

bool RouteInfo::IsIncludeCircuitBreakerInstances() {
  return route_flag_ & kGetInstancesRequestIncludeCircuitBreaker;
}

uint8_t RouteInfo::GetRequestFlags() { return route_flag_; }

void RouteInfo::SetRouterFlag(const char* router_name, bool enable) {
  if (disable_routers_ == NULL) {
    disable_routers_ = new std::set<const char*, less_for_c_strings>();
  }
  if (enable) {
    disable_routers_->erase(router_name);
  } else {
    disable_routers_->insert(router_name);
  }
}

void RouteInfo::SetRouterChainEnd(bool value) { end_route_ = value; }

bool RouteInfo::IsRouterChainEnd() { return end_route_; }

bool RouteInfo::IsRouterEnable(const char* router_name) {
  if (disable_routers_ == NULL) {  // 表示未设置，默认所有插件都启用
    return true;
  }
  return disable_routers_->find(router_name) == disable_routers_->end();
}

void RouteInfo::SetLables(const std::map<std::string, std::string>& labels) {
  if (labels_ == NULL) {
    labels_ = new std::map<std::string, std::string>();
  }
  *labels_ = labels;
}

const std::map<std::string, std::string>& RouteInfo::GetLabels() {
  return labels_ != NULL ? *labels_ : EmptyStringMap();
}

void RouteInfo::SetMetadataPara(const MetadataRouterParam& metadata_param) {
  if (metadata_param_ == NULL) {
    metadata_param_ = new MetadataRouterParam(metadata_param);
  } else {
    *metadata_param_ = metadata_param;
  }
}

const std::map<std::string, std::string>& RouteInfo::GetMetadata() {
  return metadata_param_ != NULL ? metadata_param_->metadata_ : EmptyStringMap();
}

MetadataFailoverType RouteInfo::GetMetadataFailoverType() {
  return metadata_param_ != NULL ? metadata_param_->failover_type_ : kMetadataFailoverNone;
}

///////////////////////////////////////////////////////////////////////////////
RouteResult::RouteResult() {
  service_instances_    = NULL;
  redirect_service_key_ = NULL;
}

RouteResult::~RouteResult() {
  if (service_instances_ != NULL) {
    delete service_instances_;
    service_instances_ = NULL;
  }
  if (redirect_service_key_ != NULL) {
    delete redirect_service_key_;
    redirect_service_key_ = NULL;
  }
}

void RouteResult::SetServiceInstances(ServiceInstances* service_instances) {
  service_instances_ = service_instances;
}

ServiceInstances* RouteResult::GetServiceInstances() { return service_instances_; }

ServiceInstances* RouteResult::GetAndClearServiceInstances() {
  ServiceInstances* service_instances = service_instances_;
  service_instances_                  = NULL;
  return service_instances;
}

bool RouteResult::isRedirect() { return redirect_service_key_ != NULL; }

const ServiceKey& RouteResult::GetRedirectService() { return *redirect_service_key_; }

void RouteResult::SetRedirectService(const ServiceKey& service_key) {
  if (redirect_service_key_ == NULL) {
    redirect_service_key_ = new ServiceKey();
  }
  *redirect_service_key_ = service_key;
}

void RouteResult::SetSubset(const std::map<std::string, std::string>& subset) { subset_ = subset; }

const std::map<std::string, std::string>& RouteResult::GetSubset() { return subset_; }

}  // namespace polaris
