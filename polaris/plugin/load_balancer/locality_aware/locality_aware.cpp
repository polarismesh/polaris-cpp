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

#include "plugin/load_balancer/locality_aware/locality_aware.h"

#include "context/context_impl.h"
#include "model/instance.h"
#include "model/model_impl.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "utils/time_clock.h"

namespace polaris {

static const int64_t kDefaultMinWeight = 1000;         // 节点默认的最小权值
static const int64_t kDefaultDescribeInterval = 1000;  // 默认的LA状态输出间隔（写日志中） ms
static const int kRouteKeySize = 20;                   // ROUTE_KEY位数，不能大于24位
static const int kBeginTimeSize = 64 - kRouteKeySize;  // BEGIN_TIME_MS位数
// 17,592,186,044,416 (44位无符号最大值)
//    315,360,000,000 (程序启动10年时间值，以ms计)
static const uint64_t kMaxBeginTimeMs = std::numeric_limits<uint64_t>::max() >> kRouteKeySize;
static const uint64_t kMaxRouteKey = std::numeric_limits<uint64_t>::max() >> kBeginTimeSize;

uint64_t LocalityAwareLoadBalancer::CalculateLocalityAwareInfo(uint32_t route_key, uint64_t begin_time_ms) {
  // 要确保传入参数的范围是正确的
  return (static_cast<uint64_t>(route_key) << kBeginTimeSize) + begin_time_ms;
}

uint32_t LocalityAwareLoadBalancer::GetRouteKey(uint64_t locality_aware_info) {
  return static_cast<uint32_t>(locality_aware_info >> kBeginTimeSize);  // key在高位
}

uint64_t LocalityAwareLoadBalancer::GetBeginTimeMs(uint64_t locality_aware_info) {
  return locality_aware_info & kMaxBeginTimeMs;  // time在低位, 将高位的key置零
}

ReturnCode LocalityAwareLoadBalancer::ChooseInstance(ServiceInstances *service_instances, const Criteria &criteria,
                                                     Instance *&next) {
  // 获取所有实例
  next = nullptr;
  InstancesSet *instances_set = service_instances->GetAvailableInstances();
  LocalityAwareLbCacheKey cache_key = {instances_set};
  LocalityAwareLBCacheValue *lb_value = cache_key_data_cache_->GetWithRcuTime(cache_key);
  if (lb_value == nullptr) {
    lb_value = cache_key_data_cache_->CreateOrGet(cache_key, [&]() -> LocalityAwareLBCacheValue * {
      std::vector<Instance *> instances = instances_set->GetInstances();
      LocalityAwareLBCacheValue *new_lb_value = new LocalityAwareLBCacheValue(min_weight_, instances_set);
      service_instances->GetHalfOpenInstances(new_lb_value->half_open_instances_);
      for (std::size_t i = 0; i < instances.size(); ++i) {
        Instance *&item = instances[i];
        int weight = item->GetWeight();
        // 判断是不是半开实例，不向locality_aware_selector中添加半开实例
        if (new_lb_value->half_open_instances_.find(item) == new_lb_value->half_open_instances_.end()) {
          new_lb_value->locality_aware_selector_.AddInstance(item->GetId());  // 向selector中注册实例
          new_lb_value->instance_map_[item->GetId()] = item;                  // id到instance的映射
        } else {
          weight = 1;  // 半开实例
        }
        if (weight > 0) {  // 加权随机
          new_lb_value->sum_weight_ += weight;
          LocalityAwareLBCacheValue::WeightInstance weight_instance;
          weight_instance.weight_ = new_lb_value->sum_weight_;
          weight_instance.instance_ = item;
          new_lb_value->weight_instances_.push_back(weight_instance);
        }
      }

      int retry_count = 0;
      while (++retry_count < 100) {
        new_lb_value->route_key_ = (++route_key_count_);
        if (new_lb_value->route_key_ >= kMaxRouteKey) {
          route_key_count_ = 1;  // 确保key不超上限
          new_lb_value->route_key_ = (++route_key_count_);
        }
        LocalityAwareLBCacheValue *old_cache_value = rout_key_data_cache_->GetWithRcuTime(new_lb_value->route_key_);
        if (old_cache_value == nullptr) {
          // 填写route_key，并进行缓存
          new_lb_value->IncrementRef();
          rout_key_data_cache_->CreateOrGet(new_lb_value->route_key_, [&] { return new_lb_value; });
          break;
        }
      }
      if (retry_count >= 100) {
        new_lb_value->DecrementRef();
        return nullptr;
      }
      return new_lb_value;
    });
  }

  if (lb_value == nullptr) {
    return kReturnTimeout;
  }

  if (!criteria.ignore_half_open_) {
    service_instances->GetService()->TryChooseHalfOpenInstance(lb_value->half_open_instances_, next);
    if (next != nullptr) {
      return kReturnOk;
    }
  }

  // 挑选一个实例
  SelectIn in;
  SelectOut out;
  // begin_time_ms需要传给Feedback，因为LA在跟踪未返回的调用
  // SelectInstance时计数并统计时间，Feedback时清除
  uint64_t now_time_us = Time::GetSteadyTimeUs();
  uint64_t begin_time_ms = (now_time_us - system_begin_time_) / 1000;
  if (begin_time_ms > kMaxBeginTimeMs) {
    // 时间超过上限
    POLARIS_LOG(LOG_ERROR, "locality aware begin_time_ms overflow : %ld", begin_time_ms);
  }
  in.begin_time_us = begin_time_ms * 1000 + system_begin_time_;
  in.changable_weights = true;
  ReturnCode ret = lb_value->locality_aware_selector_.SelectInstance(in, &out);

  if (ret != kReturnOk) {
    // 加权随机兜底,针对主要由非一致产生的 kReturnUnknownError
    // 以及LA内部没有实例，可能全为半开实例 kReturnInstanceNotFound
    if ((ret != kReturnUnknownError && ret != kReturnInstanceNotFound) || lb_value->sum_weight_ <= 0) {
      return ret;
    }

    // 获取一个随机数
    static __thread bool thread_local_seed_not_init = true;
    static __thread unsigned int thread_local_seed = 0;
    if (thread_local_seed_not_init) {
      thread_local_seed_not_init = false;
      thread_local_seed = time(nullptr) ^ pthread_self();
    }
    LocalityAwareLBCacheValue::WeightInstance random_weight = {rand_r(&thread_local_seed) % lb_value->sum_weight_,
                                                               nullptr};
    std::vector<LocalityAwareLBCacheValue::WeightInstance>::iterator it =
        std::upper_bound(lb_value->weight_instances_.begin(), lb_value->weight_instances_.end(), random_weight);
    next = it->instance_;

    return kReturnOk;
  }

  Instance *selected_instance = lb_value->instance_map_[out.instance_id];
  // 将LA需要传递给Feedback的信息写Instance中
  uint64_t locality_aware_info = CalculateLocalityAwareInfo(lb_value->route_key_, begin_time_ms);
  if (locality_aware_info > 0) {
    next = selected_instance->GetImpl().DumpWithLocalityAwareInfo(locality_aware_info);
  } else {
    next = selected_instance;
  }

  uint64_t expected_time = describe_time_;
  if (describe_interval_ != 0 && now_time_us > expected_time) {
    // 将LA的状态写到polaris日志
    bool ret = describe_time_.compare_exchange_strong(expected_time, now_time_us + describe_interval_ * 1000);
    if (ret == true) {
      std::string la_info;
      lb_value->locality_aware_selector_.Describe(la_info);
      POLARIS_LOG(LOG_INFO, "%s", la_info.c_str());
    }
  }

  return kReturnOk;
}

ReturnCode LocalityAwareLoadBalancer::Init(Config *config, Context *context) {
  static const char kDescribeInterval[] = "describeInterval";
  static const char kMinWeight[] = "minWeight";
  describe_interval_ = static_cast<uint32_t>(config->GetMsOrDefault(kDescribeInterval, kDefaultDescribeInterval));
  min_weight_ = static_cast<int64_t>(config->GetIntOrDefault(kMinWeight, kDefaultMinWeight));
  srand(time(nullptr));
  cache_key_data_cache_ = new ServiceCache<LocalityAwareLbCacheKey, LocalityAwareLBCacheValue>();
  rout_key_data_cache_ = new ServiceCache<uint32_t, LocalityAwareLBCacheValue>();
  context_ = context;
  context_->GetContextImpl()->RegisterCache(cache_key_data_cache_);
  context_->GetContextImpl()->RegisterCache(rout_key_data_cache_);
  route_key_count_ = 1;
  return kReturnOk;
}

ReturnCode LocalityAwareLoadBalancer::Feedback(const FeedbackInfo &info) {
  // 将la的key和time取出
  uint32_t route_key = GetRouteKey(info.locality_aware_info);
  uint64_t begin_time_us = GetBeginTimeMs(info.locality_aware_info) * 1000 + system_begin_time_;
  // 读取lb_value成功,调用la的Feedback
  LocalityAwareLBCacheValue *lb_value = rout_key_data_cache_->GetWithRcuTime(route_key);
  if (lb_value == nullptr) {  // 读取lb_value失败
    return kReturnInvalidState;
  }
  CallInfo call_info;
  call_info.call_daley = info.call_daley;
  call_info.begin_time_us = begin_time_us;
  call_info.instance_id = info.instance_id;
  lb_value->locality_aware_selector_.Feedback(call_info);

  return kReturnOk;
}

LocalityAwareLoadBalancer::LocalityAwareLoadBalancer() : describe_time_(0) {
  cache_key_data_cache_ = nullptr;
  rout_key_data_cache_ = nullptr;
  context_ = nullptr;
  route_key_count_ = 1;
  system_begin_time_ = Time::GetSteadyTimeUs();  // 缓存起始时间
  describe_interval_ = 0;
  describe_time_ = system_begin_time_;
  min_weight_ = 1000;
}

LocalityAwareLoadBalancer::~LocalityAwareLoadBalancer() {
  if (cache_key_data_cache_ != nullptr) {
    cache_key_data_cache_->SetClearHandler(0);
    cache_key_data_cache_->DecrementRef();
  }
  if (rout_key_data_cache_ != nullptr) {
    rout_key_data_cache_->SetClearHandler(0);
    rout_key_data_cache_->DecrementRef();
  }
  context_ = nullptr;
}

}  // namespace polaris