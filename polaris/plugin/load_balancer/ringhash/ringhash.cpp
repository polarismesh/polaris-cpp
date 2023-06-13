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

#include "plugin/load_balancer/ringhash/ringhash.h"

#include <stdlib.h>

#include <inttypes.h>
#include <map>
#include <string>
#include <vector>

#include "context/context_impl.h"
#include "logger.h"
#include "model/instance.h"
#include "model/model_impl.h"
#include "plugin/plugin_manager.h"
#include "polaris/config.h"
#include "polaris/model.h"
#include "utils/utils.h"

namespace polaris {

KetamaLoadBalancer::KetamaLoadBalancer()
    : context_(nullptr),
      vnode_cnt_(0),
      base_weight_(0),
      hash_func_(nullptr),
      compatible_go_(false),
      data_cache_(nullptr) {}

KetamaLoadBalancer::~KetamaLoadBalancer() {
  if (data_cache_ != nullptr) {
    data_cache_->SetClearHandler(0);
    data_cache_->DecrementRef();
  }
  context_ = nullptr;
}

ReturnCode KetamaLoadBalancer::Init(Config* config, Context* context) {
  context_ = context;

  static const char kVirtualNodeCount[] = "vnodeCount";
  static const uint32_t kVirtualNodeCountDefault = 1024;
  static const char kHashFunction[] = "hashFunc";
  static const char kHashFunctionDefault[] = "murmur3";
  static const char kCompatibleGoKey[] = "compatibleGo";
  static const bool kCompatibleGoDefault = false;
  static const char kBaseWeightKey[] = "baseWeight";
  static const int kBaseWeightDefault = 0;

  // 读配置, 加载虚拟节点数和哈希函数
  compatible_go_ = config->GetBoolOrDefault(kCompatibleGoKey, kCompatibleGoDefault);
  if (compatible_go_) {
    static const uint32_t kGoVirtualNodeCountDefault = 10;
    vnode_cnt_ = config->GetIntOrDefault(kVirtualNodeCount, kGoVirtualNodeCountDefault);
  } else {
    vnode_cnt_ = config->GetIntOrDefault(kVirtualNodeCount, kVirtualNodeCountDefault);
  }
  base_weight_ = config->GetIntOrDefault(kBaseWeightKey, kBaseWeightDefault);

  std::string hash_func = config->GetStringOrDefault(kHashFunction, kHashFunctionDefault);
  ReturnCode code = HashManager::Instance().GetHashFunction(hash_func, hash_func_);
  if (code != kReturnOk) {
    return code;
  }
  PluginManager::Instance().RegisterInstancePreUpdateHandler(KetamaLoadBalancer::OnInstanceUpdate);

  data_cache_ = new ServiceCache<RingHashCacheKey, RingHashCacheValue>();
  context_->GetContextImpl()->RegisterCache(data_cache_);

  return kReturnOk;
}

ReturnCode KetamaLoadBalancer::ChooseInstance(ServiceInstances* service_instances, const Criteria& criteria,
                                              Instance*& next) {
  InstancesSet* instances_set = service_instances->GetAvailableInstances();
  RingHashCacheKey cache_key = {instances_set, service_instances->GetDynamicWeightVersion()};
  RingHashCacheValue* lb_value = data_cache_->GetWithRcuTime(cache_key);

  if (lb_value == nullptr) {
    lb_value = data_cache_->CreateOrGet(cache_key, [&] {
      // 出现 ringhash cache 需要执行 create 时进行打印相关辅助信息日志
      POLARIS_LOG(LOG_DEBUG, "ringhash_cache run create action ns(%s) svc(%s) hash_str(%s) hash_key(%" PRIu64 ")",
                  service_instances->GetService()->GetServiceKey().namespace_.c_str(),
                  service_instances->GetService()->GetServiceKey().name_.c_str(), criteria.hash_string_.c_str(),
                  criteria.hash_key_);

      RingHashCacheValue* new_lb_value = new RingHashCacheValue();
      new_lb_value->prior_date_ = instances_set;
      new_lb_value->prior_date_->IncrementRef();
      service_instances->GetHalfOpenInstances(new_lb_value->half_open_instances_);
      auto& instances = instances_set->GetInstances();
      bool dynamic_weight = service_instances->GetDynamicWeightVersion() > 0;
      auto selector = new ContinuumSelector(hash_func_);
      if (compatible_go_) {
        selector->Setup(instances, new_lb_value->half_open_instances_, vnode_cnt_, base_weight_, dynamic_weight);
        if (selector->EmptyRing()) {
          std::set<Instance*> empty_half_open;
          selector->Setup(instances, empty_half_open, vnode_cnt_, base_weight_, dynamic_weight);
        }
      } else {
        selector->FastSetup(instances, new_lb_value->half_open_instances_, vnode_cnt_, base_weight_, dynamic_weight);
        if (selector->EmptyRing()) {
          std::set<Instance*> empty_half_open;
          selector->FastSetup(instances, empty_half_open, vnode_cnt_, base_weight_, dynamic_weight);
        }
      }
      new_lb_value->selector_.reset(selector);
      return new_lb_value;
    });
  }

  next = nullptr;
  if (!criteria.ignore_half_open_ && criteria.replicate_index_ == 0) {
    service_instances->GetService()->TryChooseHalfOpenInstance(lb_value->half_open_instances_, next);
    if (next != nullptr) {
      return kReturnOk;
    }
  }
  if (criteria.replicate_index_ <= 0) {
    int index = lb_value->selector_->Select(criteria);
    if (-1 != index) {
      const std::vector<Instance*>& instances = instances_set->GetInstances();
      next = instances[index];
      return kReturnOk;
    }
    return kReturnInstanceNotFound;
  }

  // 获取副本实例
  return lb_value->selector_->SelectReplicate(instances_set->GetInstances(), criteria, next);
}

void KetamaLoadBalancer::OnInstanceUpdate(const InstancesData* old_instances, InstancesData* new_instances) {
  auto new_it = new_instances->instances_map_.begin();
  auto new_end = new_instances->instances_map_.end();
  const std::map<std::string, Instance*>& old_instances_map = old_instances->instances_map_;
  auto old_end = old_instances->instances_map_.end();
  std::map<std::string, Instance*>::const_iterator old_it;
  for (; new_it != new_end; ++new_it) {
    if ((old_it = old_instances_map.find(new_it->second->GetId())) != old_end) {  // 迁移数据
      new_it->second->GetImpl().CopyLocalValue(old_it->second->GetImpl());
    }
  }
}

}  // namespace polaris
