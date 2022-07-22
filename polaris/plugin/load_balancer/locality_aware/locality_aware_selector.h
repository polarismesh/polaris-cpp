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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_LOCALITY_AWARE_LOCALITY_AWARE_SELECTOR_H_
#define POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_LOCALITY_AWARE_LOCALITY_AWARE_SELECTOR_H_

#include <sys/time.h>

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "logger.h"
#include "polaris/defs.h"

#include "circular_queue.h"        // 循环队列
#include "doubly_buffered_data.h"  // DoublyBufferedData

namespace polaris {

static const double kPunishInflightRatio = 1.5;

typedef std::string InstanceId;

struct SelectIn {
  // 这个时间是用来跟踪未返回调用的，与计算调用延时所用的开始时间可能不一致
  // 该值需要准确的传给Feedback()
  uint64_t begin_time_us;
  // 保留字段，目前始终填写true
  // 填写false时，且不进行Feedback，能维持权重不变
  bool changable_weights;
};

struct SelectOut {
  SelectOut() : need_feedback(false) {}
  bool need_feedback;  // 配合SelectIn中changable_weights的字段，保留
  InstanceId instance_id;
};

struct CallInfo {
  uint64_t call_daley;     // 调用的延时，由业务提供
  uint64_t begin_time_us;  // 和SelectIn中begin_time_us严格一致
  InstanceId instance_id;
};

class LocalityAwareSelector {
 public:
  explicit LocalityAwareSelector(int64_t min_weight);
  virtual ~LocalityAwareSelector();
  bool AddInstance(const InstanceId &id);
  bool RemoveInstance(const InstanceId &id);
  ReturnCode SelectInstance(const SelectIn &in, SelectOut *out);
  void Describe(std::ostream &os);
  void Describe(std::string &str);
  void Feedback(const CallInfo &info);
  void Destroy();

 private:
  struct TimeInfo {
    int64_t latency_sum;  // microseconds
    int64_t end_time_us;
  };

  class Instances;
  class Weight {
    friend class Instances;

   public:
    static const size_t kRecvQueueSize = 128;

    Weight(int64_t initial_weight, int64_t min_weight);
    ~Weight();

    // 更新_weight,并返回差值
    int64_t Update(const CallInfo &, size_t index);

    // 返回_weight
    int64_t GetWeight() const { return weight_; }

    struct AddInflightResult {
      bool chosen;
      int64_t weight_diff;
    };

    AddInflightResult AddInflight(const SelectIn &in, size_t index, int64_t dice);

    void Describe(std::ostream &os, int64_t now);
    void Describe(std::string &str, int64_t now);

    int64_t Disable();
    bool Disabled() const { return base_weight_ < 0; }
    int64_t MarkOld(size_t index);
    std::pair<int64_t, int64_t> ClearOld();

    int64_t ResetWeight(size_t index, int64_t now_us);

   private:
    int64_t weight_;       // 实际生效的weight,受min_weight等规则约束
    int64_t base_weight_;  // 根据数学模型直接计算到的weight
    int64_t min_weight_;   // 最小权重，可在yaml配置，默认1000
    std::mutex mutex_;
    int64_t begin_time_sum_;
    int begin_time_count_;
    int64_t old_diff_sum_;
    size_t old_index_;
    int64_t old_weight_;
    int64_t avg_latency_;
    CircularQueue<TimeInfo> time_q_;  // 统计调用信息所用的循环队列
  };

  struct InstanceInfo {
    InstanceInfo() : left(nullptr), weight(nullptr) {}
    InstanceId instance_id;
    std::atomic<int64_t> *left;
    Weight *weight;
  };

  class Instances {
   public:
    std::vector<InstanceInfo> weight_tree;
    std::map<InstanceId, size_t> instance_map;

   public:
    // 更新父节点的权重
    void UpdateParentWeights(int64_t diff, size_t index) const;
  };

  static bool Add(Instances &bg, const Instances &fg, InstanceId id, LocalityAwareSelector *);
  static bool Remove(Instances &bg, InstanceId id, LocalityAwareSelector *);
  static bool RemoveAll(Instances &bg, const Instances &fg);

  // Add a entry to _left_weights.
  std::atomic<int64_t> *PushLeft() {
    left_weights_.push_back(0);
    return reinterpret_cast<std::atomic<int64_t> *>(&left_weights_.back());
  }
  void PopLeft() { left_weights_.pop_back(); }

 private:
  std::atomic<int64_t> total_;
  int64_t min_weight_;
  polaris::DoublyBufferedData<Instances> db_instances_;
  std::deque<int64_t> left_weights_;
  std::atomic<uint64_t> select_failed_times_;
};

inline void LocalityAwareSelector::Instances::UpdateParentWeights(int64_t diff, size_t index) const {
  while (index != 0) {
    const size_t parent_index = (index - 1) >> 1;
    if ((parent_index << 1) + 1 == index) {  //左孩子
      *(weight_tree[parent_index].left) += diff;
    }
    index = parent_index;
  }
}

inline int64_t LocalityAwareSelector::Weight::ResetWeight(size_t index, int64_t now_us) {
  int64_t new_weight = base_weight_;
  if (begin_time_count_ > 0) {
    const int64_t inflight_delay = now_us - begin_time_sum_ / begin_time_count_;
    const int64_t punish_latency = (int64_t)(avg_latency_ * kPunishInflightRatio);
    if (inflight_delay >= punish_latency && avg_latency_ > 0) {
      new_weight = new_weight * punish_latency / inflight_delay;
    }
  }
  if (new_weight < min_weight_) {
    new_weight = min_weight_;
  }
  const int64_t old_weight = weight_;
  weight_ = new_weight;
  const int64_t diff = new_weight - old_weight;
  if (old_index_ == index && diff != 0) {
    old_diff_sum_ += diff;
  }
  return diff;
}

inline LocalityAwareSelector::Weight::AddInflightResult LocalityAwareSelector::Weight::AddInflight(const SelectIn &in,
                                                                                                   size_t index,
                                                                                                   int64_t dice) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (Disabled()) {
    AddInflightResult r;
    r.chosen = false;
    r.weight_diff = 0;
    return r;
  }
  const int64_t diff = ResetWeight(index, in.begin_time_us);
  if (weight_ < dice) {
    // inflight delay makes the weight too small to choose.
    AddInflightResult r;
    r.chosen = false;
    r.weight_diff = diff;
    return r;
  }
  begin_time_sum_ += in.begin_time_us;
  ++begin_time_count_;
  AddInflightResult r;
  r.chosen = true;
  r.weight_diff = diff;
  return r;
}

}  // namespace polaris

#endif  // POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_LOCALITY_AWARE_LOCALITY_AWARE_SELECTOR_H_