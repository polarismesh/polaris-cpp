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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_WEIGHT_ADJUSTER_SLOW_START_H_
#define POLARIS_CPP_POLARIS_PLUGIN_WEIGHT_ADJUSTER_SLOW_START_H_

#include "plugin/weight_adjuster/weight_adjuster.h"

#include <map>
#include <mutex>

#include "reactor/reactor.h"

namespace polaris {

class SlowStartWeightAdjuster : public WeightAdjuster {
 public:
  SlowStartWeightAdjuster();

  virtual ~SlowStartWeightAdjuster();

  virtual ReturnCode Init(Config* config, Context* context);

  // 服务实例列表更新
  virtual ReturnCode ServiceInstanceUpdate(ServiceData* new_service_data, ServiceData* old_service_data);

  virtual bool DoAdjust(ServiceData* service_data);

  std::map<std::string, uint64_t> GetSlowStartTasks();

  static double AggressionFactor(double time_factor, double aggression);

 private:
  Context* context_;

  // 慢启动创建
  uint64_t window_;
  // 调整步长
  uint64_t step_size_;
  // 慢启动速率
  double aggression_;
  // 慢启动初始权重百分比
  double min_weight_percent_;

  // 调整定时任务
  std::mutex lock_;
  std::map<std::string, uint64_t> slow_start_map_;
};

// 定时调整任务
class SlowStartAdjustTask : public TimingTask {
 public:
  SlowStartAdjustTask(Context* context, const ServiceKey& service_key, uint64_t interval)
      : TimingTask(interval), context_(context), service_key_(service_key), step_size_(interval) {}

  virtual ~SlowStartAdjustTask() {}

  virtual void Run();

  virtual uint64_t NextRunTime();

  void Submit();

  bool DoAdjustWithRcuTime();

 private:
  Context* context_;
  ServiceKey service_key_;
  uint64_t step_size_;
};

// 定时任务提交
class SlowStartAdjustSubmit : public Task {
 public:
  explicit SlowStartAdjustSubmit(SlowStartAdjustTask* task) : task_(task) {}

  virtual ~SlowStartAdjustSubmit() {}

  virtual void Run() {
    task_->Submit();
    task_.release();
  }

 private:
  std::unique_ptr<SlowStartAdjustTask> task_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_WEIGHT_ADJUSTER_SLOW_START_H_
