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
#include <iosfwd>
#include <vector>

#include "context_internal.h"
#include "model/model_impl.h"
#include "polaris/config.h"
#include "polaris/context.h"

namespace polaris {

RandomLoadBalancer::RandomLoadBalancer() {
  enable_dynamic_weight_ = false;
  data_cache_            = NULL;
  context_               = NULL;
}

RandomLoadBalancer::~RandomLoadBalancer() {
  if (data_cache_ != NULL) {
    data_cache_->SetClearHandler(0);
    data_cache_->DecrementRef();
  }
  context_ = NULL;
}

ReturnCode RandomLoadBalancer::Init(Config* config, Context* context) {
  static const char kEnableDynamicWeightKey[]   = "enableDynamicWeight";
  static const bool kEnableDynamicWeightDefault = false;
  srand(time(NULL));
  enable_dynamic_weight_ =
      config->GetBoolOrDefault(kEnableDynamicWeightKey, kEnableDynamicWeightDefault);
  data_cache_ = new ServiceCache<RandomLbCacheKey>();
  context_    = context;
  context_->GetContextImpl()->RegisterCache(data_cache_);
  return kReturnOk;
}

ReturnCode RandomLoadBalancer::ChooseInstance(ServiceInstances* service_instances,
                                              const Criteria& criteria, Instance*& next) {
  // 获取所有实例
  next                        = NULL;
  InstancesSet* instances_set = service_instances->GetAvailableInstances();
  RandomLbCacheKey cache_key  = {instances_set};
  ServiceBase* cache_value    = data_cache_->GetWithRef(cache_key);
  RandomLbCacheValue* lb_value;
  if (cache_value != NULL) {
    lb_value = dynamic_cast<RandomLbCacheValue*>(cache_value);
    if (lb_value == NULL) {
      cache_value->DecrementRef();
      return kReturnInvalidState;
    }
  } else {
    std::vector<Instance*> instances = instances_set->GetInstances();
    lb_value                         = new RandomLbCacheValue();
    lb_value->prior_date_            = instances_set;
    lb_value->prior_date_->IncrementRef();
    lb_value->sum_weight_ = 0;
    service_instances->GetHalfOpenInstances(lb_value->half_open_instances_);
    lb_value->weight_instances_.reserve(instances.size());
    for (std::size_t i = 0; i < instances.size(); ++i) {
      Instance*& item = instances[i];
      // 判断是否获取动态权重
      int weight = enable_dynamic_weight_ ? item->GetDynamicWeight() : item->GetWeight();
      // 半开实例，修改权重为1，仍然加入分配。这样全部为半开实例时仍然有实例可以分配
      if (lb_value->half_open_instances_.find(item) != lb_value->half_open_instances_.end()) {
        weight = 1;
      }
      if (weight > 0) {
        lb_value->sum_weight_ += weight;
        WeightInstance weight_instance;
        weight_instance.weight_   = lb_value->sum_weight_;
        weight_instance.instance_ = item;
        lb_value->weight_instances_.push_back(weight_instance);
      }
    }
    data_cache_->PutWithRef(cache_key, lb_value);
  }

  if (!criteria.ignore_half_open_) {
    service_instances->GetService()->TryChooseHalfOpenInstance(lb_value->half_open_instances_,
                                                               next);
    if (next != NULL) {
      lb_value->DecrementRef();
      return kReturnOk;
    }
  }

  if (lb_value->sum_weight_ <= 0) {
    lb_value->DecrementRef();
    return kReturnInstanceNotFound;
  }

  // 获取一个随机数
  static __thread bool thread_local_seed_not_init = true;
  static __thread unsigned int thread_local_seed  = 0;
  if (thread_local_seed_not_init) {
    thread_local_seed_not_init = false;
    thread_local_seed          = time(NULL) ^ pthread_self();
  }
  WeightInstance random_weight = {rand_r(&thread_local_seed) % lb_value->sum_weight_, NULL};
  std::vector<WeightInstance>::iterator it = std::upper_bound(
      lb_value->weight_instances_.begin(), lb_value->weight_instances_.end(), random_weight);
  next = it->instance_;

  lb_value->DecrementRef();
  return kReturnOk;
}

}  // namespace polaris
