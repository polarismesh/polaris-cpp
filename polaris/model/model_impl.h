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

#ifndef POLARIS_CPP_POLARIS_MODEL_MODEL_IMPL_H_
#define POLARIS_CPP_POLARIS_MODEL_MODEL_IMPL_H_

#include <pthread.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "logger.h"
#include "plugin/load_balancer/hash/hash_manager.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "quota/model/service_rate_limit_rule.h"
#include "sync/cond_var.h"
#include "utils/ref_count.h"
#include "v1/request.pb.h"
#include "v1/response.pb.h"

namespace polaris {

const char* DataTypeToStr(ServiceDataType data_type);

/// @desc 负载均衡选择子接口类，返回值均为实例下标
class Selector {
 public:
  virtual ~Selector() {}

  virtual int Select(const Criteria& criteria) = 0;
};

class InstancesSetImpl {
 public:
  explicit InstancesSetImpl(const std::vector<Instance*>& instances)
      : count_(0), instances_(instances), recover_all_(false) {}

  InstancesSetImpl(const std::vector<Instance*>& instances, const std::map<std::string, std::string>& subset)
      : count_(0), instances_(instances), subset_(subset), recover_all_(false) {}

  InstancesSetImpl(const std::vector<Instance*>& instances, const std::map<std::string, std::string>& subset,
                   const std::string& recover_info)
      : count_(0), instances_(instances), subset_(subset), recover_all_(false), recover_info_(recover_info) {}

  bool UpdateRecoverAll(bool recover_all);

  static uint64_t CalcTotalWeight(const std::vector<Instance*>& instances);

  static uint32_t CalcMaxWeight(const std::vector<Instance*>& instances);

  std::mutex& CreationLock() { return selector_creation_mutex_; }

 public:
  std::atomic<int> count_;  // 记录这个Set被访问的次数

 private:
  friend class InstancesSet;
  std::vector<Instance*> instances_;
  std::map<std::string, std::string> subset_;  // 所属的subset
  std::atomic<bool> recover_all_;  // 用来标记这个集合计算的下一个路由是否发生了全死全活
  std::string recover_info_;
  std::unique_ptr<Selector> selector_;
  std::mutex selector_creation_mutex_;
};

class InstancesData {
 public:
  InstancesData()
      : is_enable_nearby_(false), is_enable_canary_(false), instances_(nullptr), dynamic_weight_version_(0) {}

  ~InstancesData() {
    instances_->DecrementRef();
    for (auto& it : instances_map_) {
      delete it.second;
    }
    for (auto& instance : isolate_instances_) {
      delete instance;
    }
  }
  std::map<std::string, std::string> metadata_;
  bool is_enable_nearby_;
  bool is_enable_canary_;
  std::map<std::string, Instance*> instances_map_;
  std::set<Instance*> unhealthy_instances_;
  std::set<Instance*> isolate_instances_;
  InstancesSet* instances_;
  std::atomic<uint64_t> dynamic_weight_version_;
};

class ServiceInstances::Impl {
 public:
  explicit Impl(ServiceData* service_data);
  ServiceData* service_data_;
  InstancesSet* available_instances_;
  InstancesData* data_;
  uint64_t dynamic_weight_version_;
};

struct ServiceKeyWithType {
  ServiceDataType data_type_;
  ServiceKey service_key_;
};

inline bool operator<(ServiceKeyWithType const& lhs, ServiceKeyWithType const& rhs) {
  if (lhs.data_type_ == rhs.data_type_) {
    return lhs.service_key_ < rhs.service_key_;
  } else {
    return lhs.data_type_ < rhs.data_type_;
  }
}

struct RouteRuleData;

class ServiceDataImpl {
 public:
  // 解析服务实例数据
  void ParseInstancesData(v1::DiscoverResponse& response);

  // 解析服务路由数据
  void ParseRouteRuleData(v1::DiscoverResponse& response);
  void FillSystemVariables(const SystemVariables& variables);

  // 解析服务限流规则数据
  void ParseRateLimitData(v1::DiscoverResponse& response);

  // 熔断配置
  void ParseCircuitBreaker(v1::DiscoverResponse& response);

  RouteRuleData* GetRouteRuleData() { return data_.route_rule_; }

  RateLimitData* GetRateLimitData() { return data_.rate_limit_; }

  v1::CircuitBreaker* GetCircuitBreaker() { return data_.circuitBreaker_; }

  /**
   * @desc 处理哈希冲突
   *
   * @return uint64_t 0 - 处理失败, ow - 可用的哈希值
   */
  uint64_t HandleHashConflict(const std::map<uint64_t, Instance*>& hashMap, const ::v1::Instance& instance_data,
                              Hash64Func hashFunc);

 private:
  friend class ServiceInstances;
  friend class ServiceRouteRule;
  friend class ServiceData;
  friend class Service;
  friend class PluginManager;
  friend class InMemoryRegistry;
  ServiceKey service_key_;
  std::string revision_;
  uint64_t cache_version_;

  ServiceDataType data_type_;
  ServiceDataStatus data_status_;
  std::string json_content_;
  uint64_t available_time_;

  union {
    InstancesData* instances_;
    RouteRuleData* route_rule_;
    RateLimitData* rate_limit_;
    v1::CircuitBreaker* circuitBreaker_;
  } data_;

  Service* service_;
};

class ConditionVariableDataNotify : public DataNotify {
 public:
  ConditionVariableDataNotify() {}

  virtual ~ConditionVariableDataNotify() {}

  virtual void Notify() { data_loaded_.NotifyAll(); }

  virtual bool Wait(uint64_t timeout) { return data_loaded_.WaitFor(timeout); }

 private:
  sync::CondVarNotify data_loaded_;
};

class ServiceDataNotifyImpl {
 public:
  ServiceDataNotifyImpl(const ServiceKey& service_key, ServiceDataType data_type);
  ~ServiceDataNotifyImpl();

 private:
  friend class ServiceDataNotify;
  ServiceKey service_key_;
  ServiceDataType data_type_;
  DataNotify* data_notify_;
  std::mutex service_data_lock_;
  ServiceData* service_data_;
};

class ServiceImpl {
 public:
  ServiceImpl(const ServiceKey& service_key, uint32_t service_id);
  ~ServiceImpl();

  // 更新服务实例数据的本地ID
  void UpdateInstanceId(ServiceData* service_data);

 private:
  friend class Service;
  ServiceKey service_key_;
  uint32_t service_id_;
  uint32_t instance_next_id_;
  std::map<std::string, uint64_t> instance_id_map_;

  // 熔断数据
  pthread_rwlock_t circuit_breaker_data_lock_;
  volatile uint64_t circuit_breaker_data_version_;
  std::map<std::string, int> half_open_instances_;
  std::set<std::string> open_instances_;

  // 半开优先分配数据
  std::mutex half_open_lock_;
  std::atomic<uint64_t> last_half_open_time_;
  std::atomic<int> try_half_open_count_;
  bool have_half_open_data_;
  std::map<std::string, int> half_open_data_;  // 存储半开分配数据

  // 动态权重数据
  pthread_rwlock_t dynamic_weights_data_lock_;
  volatile uint64_t dynamic_weights_version_;
  uint64_t dynamic_weights_data_last_update_time_;
  DynamicWeightDataStatus dynamic_weights_data_status_;
  uint64_t dynamic_weights_data_sync_interval_;
  std::map<std::string, uint32_t> dynamic_weights_;
  uint64_t min_dynamic_weight_for_init_;

  // set 熔断数据
  pthread_rwlock_t sets_circuit_breaker_data_lock_;
  volatile uint64_t sets_circuit_breaker_data_version_;
  std::map<std::string, SetCircuitBreakerUnhealthyInfo> circuit_breaker_unhealthy_sets_;
};

struct SubSetInfo {
  std::map<std::string, std::string> subset_map_;
  std::string subset_info_str;

  std::string GetSubInfoStrId();
};

struct Labels {
  std::map<std::string, std::string> labels_;
  std::string labels_str;

  std::string GetLabelStr();
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_MODEL_IMPL_H_
