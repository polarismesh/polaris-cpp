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

#include "plugin/load_balancer/weighted_random.h"

#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#include <algorithm>
#include <vector>

#include "context/context_impl.h"
#include "model/model_impl.h"
#include "polaris/config.h"
#include "polaris/context.h"

namespace polaris {

RandomLoadBalancer::RandomLoadBalancer() : enable_dynamic_weight_(false), context_(nullptr), data_cache_(nullptr) {}

RandomLoadBalancer::~RandomLoadBalancer() {
  if (data_cache_ != nullptr) {
    data_cache_->SetClearHandler(0);
    data_cache_->DecrementRef();
  }
}

ReturnCode RandomLoadBalancer::Init(Config* config, Context* context) {
  constexpr char kEnableDynamicWeightKey[] = "enableDynamicWeight";
  constexpr bool kEnableDynamicWeightDefault = false;
  enable_dynamic_weight_ = config->GetBoolOrDefault(kEnableDynamicWeightKey, kEnableDynamicWeightDefault);
  data_cache_ = new ServiceCache<RandomLbCacheKey, RandomLbCacheValue>();
  context_ = context;
  context_->GetContextImpl()->RegisterCache(data_cache_);
  return kReturnOk;
}

RandomLbCacheKey RandomLoadBalancer::GenCacheKey(ServiceInstances* service_instances) {
  return {service_instances->GetAvailableInstances(), 0};
}

ReturnCode RandomLoadBalancer::ChooseInstance(ServiceInstances* service_instances, const Criteria& criteria,
                                              Instance*& next) {
  RandomLbCacheKey cache_key = GenCacheKey(service_instances);
  RandomLbCacheValue* lb_value = data_cache_->GetWithRcuTime(cache_key);
  if (lb_value == nullptr) {
    lb_value = data_cache_->CreateOrGet(cache_key, [&] {
      RandomLbCacheValue* new_lb_value = new RandomLbCacheValue();
      InstancesSet* instances_set = service_instances->GetAvailableInstances();  // 获取所有实例
      new_lb_value->prior_date_ = instances_set;
      new_lb_value->prior_date_->IncrementRef();
      new_lb_value->sum_weight_ = 0;
      service_instances->GetHalfOpenInstances(new_lb_value->half_open_instances_);
      std::vector<Instance*> instances = instances_set->GetInstances();
      new_lb_value->weight_instances_.reserve(instances.size());
      for (auto& instance : instances) {
        // 判断是否获取动态权重
        int weight = enable_dynamic_weight_ ? instance->GetDynamicWeight() : instance->GetWeight();
        if (new_lb_value->half_open_instances_.count(instance) == 0 && weight > 0) {  // 非半开实例和权重大于0实例
          new_lb_value->sum_weight_ += weight;
          new_lb_value->weight_instances_.push_back({new_lb_value->sum_weight_, instance});
        }
      }
      if (new_lb_value->sum_weight_ == 0) {  // 没有正常实例，则使用半开实例
        for (auto& instance : new_lb_value->half_open_instances_) {
          int weight = enable_dynamic_weight_ ? instance->GetDynamicWeight() : instance->GetWeight();
          if (weight > 0) {
            new_lb_value->sum_weight_ += weight;
            new_lb_value->weight_instances_.push_back({new_lb_value->sum_weight_, instance});
          }
        }
      }
      return new_lb_value;
    });
  }

  next = nullptr;
  if (!criteria.ignore_half_open_) {
    service_instances->GetService()->TryChooseHalfOpenInstance(lb_value->half_open_instances_, next);
    if (next != nullptr) {
      return kReturnOk;
    }
  }

  if (lb_value->sum_weight_ <= 0) {
    return kReturnInstanceNotFound;
  }

  // 获取一个随机数
  static __thread bool thread_local_seed_not_init = true;
  static __thread unsigned int thread_local_seed = 0;
  if (thread_local_seed_not_init) {
    thread_local_seed_not_init = false;
    thread_local_seed = time(nullptr) ^ pthread_self();
  }
  WeightInstance random_weight = {rand_r(&thread_local_seed) % lb_value->sum_weight_, nullptr};
  auto it = std::upper_bound(lb_value->weight_instances_.begin(), lb_value->weight_instances_.end(), random_weight);
  next = it->instance_;
  return kReturnOk;
}

}  // namespace polaris
