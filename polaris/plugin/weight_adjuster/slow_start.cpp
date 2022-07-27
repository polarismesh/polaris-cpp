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

#include "plugin/weight_adjuster/slow_start.h"

#include <cmath>

#include "context/context_impl.h"
#include "context/service_context.h"
#include "logger.h"
#include "model/instance.h"
#include "utils/time_clock.h"

namespace polaris {

SlowStartWeightAdjuster::SlowStartWeightAdjuster()
    : context_(nullptr), window_(0), step_size_(0), aggression_(0), min_weight_percent_(0) {}

SlowStartWeightAdjuster::~SlowStartWeightAdjuster() {}

ReturnCode SlowStartWeightAdjuster::Init(Config* config, Context* context) {
  context_ = context;
  if ((window_ = config->GetMsOrDefault("window", 60 * 1000)) < 10 * 1000) {
    window_ = 10 * 1000;
    POLARIS_LOG(LOG_WARN, "window must bigger than 10s");
  }
  if ((step_size_ = config->GetIntOrDefault("stepSize", 10 * 1000)) < 1000) {
    step_size_ = 1000;
    POLARIS_LOG(LOG_WARN, "step size must bigger than 1s");
  }
  aggression_ = config->GetFloatOrDefault("aggression", 1.0);
  if (aggression_ > 1.0 || aggression_ <= 0.0) {
    aggression_ = 1.0;
    POLARIS_LOG(LOG_WARN, "aggression must be (0, 1.0]");
  }
  min_weight_percent_ = config->GetFloatOrDefault("minWeightPercent", 0.1);

  return kReturnOk;
}

ReturnCode SlowStartWeightAdjuster::ServiceInstanceUpdate(ServiceData* new_service_data,
                                                          ServiceData* old_service_data) {
  if (new_service_data == nullptr || old_service_data == nullptr) {
    return kReturnOk;  // 第一次获取服务实例，由于没有返回服务实例创建时间，不做慢启动处理
  }
  ServiceInstances old_instances(old_service_data);
  ServiceInstances new_instances(new_service_data);
  auto& old_instance_map = old_instances.GetInstances();
  auto& new_instance_map = old_instances.GetInstances();
  if (old_instance_map.empty() || new_instance_map.empty()) {
    return kReturnOk;
  }
  std::set<Instance*> new_add_instances;
  for (auto& new_it : new_instances.GetInstances()) {
    if (old_instance_map.count(new_it.first) == 0) {
      new_add_instances.insert(new_it.second);
    }
  }
  if (new_add_instances.empty()) {
    return kReturnOk;  // 没有新增实例，退出
  }

  uint64_t current_time = Time::GetCoarseSteadyTimeMs();
  std::map<std::string, uint64_t> new_slow_start_task;
  for (auto& instance : new_add_instances) {  // 更新初始动态权重
    instance->GetImpl().SetDynamicWeight(instance->GetWeight() * min_weight_percent_);
    POLARIS_LOG(LOG_INFO, "adjust weight for instance[%s:%d] dynamic[%d] static[%d]", instance->GetHost().c_str(),
                instance->GetPort(), instance->GetDynamicWeight(), instance->GetWeight());
    new_slow_start_task.insert(std::make_pair(instance->GetId(), current_time));
  }

  // 添加实例ID到慢启动列表
  bool need_create_task = false;
  if (!new_add_instances.empty()) {
    std::lock_guard<std::mutex> lock_guard(lock_);
    need_create_task = slow_start_map_.empty();
    for (auto& task : new_slow_start_task) {
      slow_start_map_.insert(std::move(task));
    }
  }

  if (need_create_task) {  // 设置定时调整任务
    SlowStartAdjustTask* task = new SlowStartAdjustTask(context_, new_service_data->GetServiceKey(), step_size_);
    context_->GetContextImpl()->GetCacheManager()->GetReactor().SubmitTask(new SlowStartAdjustSubmit(task));
  }

  new_instances.CommitDynamicWeightVersion(old_instances.GetDynamicWeightVersion() + 1);
  return kReturnOk;
}

std::map<std::string, uint64_t> SlowStartWeightAdjuster::GetSlowStartTasks() {
  std::lock_guard<std::mutex> lock_guard(lock_);
  return slow_start_map_;
}

bool SlowStartWeightAdjuster::DoAdjust(ServiceData* service_data) {
  ServiceInstances service_instances(service_data);

  std::map<std::string, uint64_t> slow_start_task = GetSlowStartTasks();
  std::set<std::string> deleted_instances;

  uint64_t current_time = Time::GetCoarseSteadyTimeMs();
  auto& instances = service_instances.GetInstances();
  for (auto& instance_task : slow_start_task) {
    auto instance_it = instances.find(instance_task.first);
    if (instance_it == instances.end()) {
      deleted_instances.insert(instance_task.first);
      continue;
    }
    auto& instance = instance_it->second;
    auto create_duration = current_time - instance_task.second;
    if (create_duration < window_) {
      auto time_factor = static_cast<double>(create_duration) / window_;
      auto factor = std::max(AggressionFactor(time_factor, aggression_), min_weight_percent_);
      instance->GetImpl().SetDynamicWeight(instance->GetWeight() * factor);
    } else {
      instance->GetImpl().SetDynamicWeight(instance->GetWeight());
      deleted_instances.insert(instance_task.first);
    }
    POLARIS_LOG(LOG_INFO, "adjust weight for instance[%s:%d] dynamic[%d] static[%d]", instance->GetHost().c_str(),
                instance->GetPort(), instance->GetDynamicWeight(), instance->GetWeight());
  }

  bool need_continue = true;
  if (!deleted_instances.empty()) {
    std::lock_guard<std::mutex> lock_guard(lock_);
    for (auto& instance_id : deleted_instances) {
      slow_start_map_.erase(instance_id);
    }
    need_continue = !slow_start_map_.empty();
  }
  return need_continue;
}

double SlowStartWeightAdjuster::AggressionFactor(double time_factor, double aggression) {
  if (aggression == 1.0 || time_factor == 1.0) {
    return time_factor;
  } else {
    return std::pow(time_factor, 1.0 / aggression);
  }
}

void SlowStartAdjustTask::Run() {
  // 查找到对应的服务实例进行调整
  ContextImpl* context_impl = context_->GetContextImpl();
  context_impl->RcuEnter();
  if (!DoAdjustWithRcuTime()) {
    step_size_ = 0;
  }
  context_impl->RcuExit();
}

uint64_t SlowStartAdjustTask::NextRunTime() { return step_size_ > 0 ? Time::GetCoarseSteadyTimeMs() + step_size_ : 0; }

void SlowStartAdjustTask::Submit() { context_->GetContextImpl()->GetCacheManager()->GetReactor().AddTimingTask(this); }

bool SlowStartAdjustTask::DoAdjustWithRcuTime() {
  ContextImpl* context_impl = context_->GetContextImpl();
  ServiceContext* service_context = context_impl->GetServiceContextMap().GetWithRcuTime(service_key_);
  if (service_context == nullptr) {
    return false;
  }
  ServiceData* service_data = service_context->GetInstances();
  if (service_data == nullptr) {
    return false;
  }
  WeightAdjuster* weight_adjuster = service_context->GetWeightAdjuster();
  if (weight_adjuster == nullptr) {
    return false;
  }
  bool result = weight_adjuster->DoAdjust(service_data);
  // 只要有执行调整，必然有修改权重数据，触发缓存更新
  ServiceInstances service_instances(service_data);
  uint64_t new_dynamic_weight_version = service_instances.GetDynamicWeightVersion() + 1;
  service_context->BuildCacheForDynamicWeight(service_key_, new_dynamic_weight_version);
  service_instances.CommitDynamicWeightVersion(new_dynamic_weight_version);
  return result;
}

}  // namespace polaris
