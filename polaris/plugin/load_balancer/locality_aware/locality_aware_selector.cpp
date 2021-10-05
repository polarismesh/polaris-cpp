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

#include <stdlib.h>
#include <limits>

#include "context_internal.h"
#include "model/model_impl.h"
#include "polaris/config.h"
#include "polaris/context.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

#include "locality_aware_selector.h"

namespace polaris {

static const int64_t kDefaultQPS           = 1;
static const size_t kInitialWeightTreeSize = 128;
// 1008680231
static const int64_t kWeightScale =
    std::numeric_limits<int64_t>::max() / 72000000 / (kInitialWeightTreeSize - 1);

LocalityAwareSelector::LocalityAwareSelector(int64_t min_weight)
    : total_(0), min_weight_(min_weight), select_failed_times_(0) {}

LocalityAwareSelector::~LocalityAwareSelector() { db_instances_.ModifyWithForeground(RemoveAll); }

bool LocalityAwareSelector::Add(Instances &bg, const Instances &fg, InstanceId id,
                                LocalityAwareSelector *lb) {
  if (bg.weight_tree.capacity() < kInitialWeightTreeSize) {
    bg.weight_tree.reserve(kInitialWeightTreeSize);
  }
  if (bg.instance_map.find(id) != bg.instance_map.end()) {
    // 实例id已经存在
    return false;
  }
  std::map<polaris::InstanceId, size_t>::const_iterator iter = fg.instance_map.find(id);
  if (iter == fg.instance_map.end()) {
    // 前后台都没有这个实例，先创建一个然后插入后台
    // 切换前后台后，再拷贝指针，插入一次
    const size_t index = bg.weight_tree.size();
    // 如果la中原本没有实例，给默认权重,否则取平均值
    int64_t initial_weight = kWeightScale;
    if (!bg.weight_tree.empty()) {
      initial_weight = lb->total_ / bg.weight_tree.size();
    }
    // 将实例的index放instance_map，方便查找，判断实例是否存在
    bg.instance_map[id] = index;
    // 在weight_tree中添加节点
    InstanceInfo info;
    info.instance_id = id;
    info.left        = lb->PushLeft();
    info.weight      = new Weight(initial_weight, lb->min_weight_);
    bg.weight_tree.push_back(info);
    // 根据权重更新数据，left值
    const int64_t diff = info.weight->GetWeight();
    if (diff) {
      bg.UpdateParentWeights(diff, index);
      lb->total_ += diff;
    }
  } else {
    // 另一个buffer已经改了，进行同步，拷贝指针并添加到weight_tree
    bg.instance_map[id] = bg.weight_tree.size();
    bg.weight_tree.push_back(fg.weight_tree[iter->second]);
  }
  return true;
}

bool LocalityAwareSelector::Remove(Instances &bg, InstanceId id, LocalityAwareSelector *lb) {
  std::map<polaris::InstanceId, size_t>::iterator iter = bg.instance_map.find(id);
  if (iter == bg.instance_map.end()) {
    // The id does not exist.
    return false;
  }
  // 保留节点的index并将其从instance_map中移除
  const size_t index = iter->second;
  bg.instance_map.erase(id);

  Weight *w = bg.weight_tree[index].weight;
  // Set the weight to 0. Before we change weight of the parent nodes,
  // SelectServer may still go to the node, but when it sees a zero weight,
  // it retries, as if this range of weight is removed.
  const int64_t rm_weight = w->Disable();
  if (index + 1 == bg.weight_tree.size()) {
    // last node. Removing is easier.
    bg.weight_tree.pop_back();
    if (rm_weight) {
      // The first buffer. Remove the weight from parents to disable
      // traffic going to this node. We can't remove the left_weight
      // entry because the foreground buffer does not pop last node yet
      // and still needs the left_weight (which should be same size with
      // the tree). We can't delete the weight structure for same reason.
      int64_t diff = -rm_weight;
      bg.UpdateParentWeights(diff, index);
      lb->total_ += diff;
    } else {
      // the second buffer. clean left stuff.
      delete w;
      lb->PopLeft();
    }
  } else {
    // Move last node to position `index' to fill the space.
    // 没有交换left，left在_left_weights的存放和weight_tree的位置相匹配
    bg.weight_tree[index].instance_id                  = bg.weight_tree.back().instance_id;
    bg.weight_tree[index].weight                       = bg.weight_tree.back().weight;
    bg.instance_map[bg.weight_tree[index].instance_id] = index;
    bg.weight_tree.pop_back();

    Weight *w2 = bg.weight_tree[index].weight;  // previously back()
    if (rm_weight) {
      // First buffer.
      // We need to remove the weight of last node from its parent
      // nodes and add the weight to parent nodes of node `index'.
      // However this process is not atomic. The foreground buffer still
      // sees w2 as last node and it may change the weight during the
      // process. To solve this problem, we atomically reset the weight
      // and remember the previous index (back()) in _old_index. Later
      // change to weight will add the diff to _old_diff_sum if _old_index
      // matches the index which SelectServer is from. In this way we
      // know the weight diff from foreground before we later modify it.
      const int64_t add_weight = w2->MarkOld(bg.weight_tree.size());

      // Add the weight diff to parent nodes of node `index'. Notice
      // that we don't touch parent nodes of last node here because
      // foreground is still sending traffic to last node.
      const int64_t diff = add_weight - rm_weight;
      if (diff) {
        bg.UpdateParentWeights(diff, index);
        lb->total_ += diff;
      }
      // At this point, the foreground distributes traffic to nodes
      // correctly except node `index' because weight of the node is 0.
    } else {
      // Second buffer.
      // Reset _old_* fields and get the weight change by SelectServer()
      // after MarkOld().
      const std::pair<int64_t, int64_t> p = w2->ClearOld();
      // Add the diff to parent nodes of node `index'
      const int64_t diff = p.second;
      if (diff) {
        bg.UpdateParentWeights(diff, index);
      }
      // Remove weight from parent nodes of last node.
      int64_t old_weight = -p.first - p.second;
      if (old_weight) {
        bg.UpdateParentWeights(old_weight, bg.weight_tree.size());
      }
      lb->total_ += (-p.first);
      // Clear resources.
      delete w;
      lb->PopLeft();
    }
  }
  return true;
}

bool LocalityAwareSelector::RemoveAll(Instances &bg, const Instances &fg) {
  bg.instance_map.clear();
  if (!fg.weight_tree.empty()) {
    for (size_t i = 0; i < bg.weight_tree.size(); ++i) {
      delete bg.weight_tree[i].weight;
    }
  }
  bg.weight_tree.clear();
  return true;
}

bool LocalityAwareSelector::AddInstance(const InstanceId &id) {
  POLARIS_LOG(LOG_INFO, "locality aware selector add instance: instance_id = %s", id.c_str());
  return db_instances_.ModifyWithForeground(Add, id, this);
}

bool LocalityAwareSelector::RemoveInstance(const InstanceId &id) {
  POLARIS_LOG(LOG_INFO, "locality aware selector remove instance: instance_id = %s", id.c_str());
  return db_instances_.Modify(Remove, id, this);
}

ReturnCode LocalityAwareSelector::SelectInstance(const SelectIn &in, SelectOut *out) {
  DoublyBufferedData<Instances>::ScopedPtr s;
  if (db_instances_.Read(&s) != 0) {
    return kReturnResourceNotFound;
  }
  const size_t n = s->weight_tree.size();
  if (n == 0) {
    return kReturnInstanceNotFound;
  }
  size_t ntry   = 0;
  size_t nloop  = 0;
  int64_t total = total_;

  // 获取一个随机数
  static __thread bool thread_local_seed_not_init = true;
  static __thread unsigned int thread_local_seed  = 0;
  if (thread_local_seed_not_init) {
    thread_local_seed_not_init = false;
    thread_local_seed          = time(NULL) ^ pthread_self();
  }
  double dice_proportion = static_cast<double>(rand_r(&thread_local_seed)) / RAND_MAX;
  int64_t dice           = total * dice_proportion;

  size_t index = 0;
  int64_t self = 0;
  while (total > 0) {
    if (++nloop > 10000) {
      // 循环次数过多，非正常状态，大概率是有bug
      POLARIS_LOG(LOG_ERROR, "a locality aware selection runs too long!");
      ++select_failed_times_;
      return kReturnUnknownError;
    }

    // 在树中找到一个节点，这不是原子的,为获得更多并行性
    // 节点的左权重/总权重/节点权重可能不一致,在其他线程添加或删除节点时
    // 最终状态是符合一致性的
    const InstanceInfo &info = s->weight_tree[index];
    const int64_t left       = *(info.left);
    if (dice < left) {
      index = index * 2 + 1;
      if (index < n) {
        continue;
      }
    } else if (dice >= left + (self = info.weight->GetWeight())) {
      dice -= left + self;
      index = index * 2 + 2;
      if (index < n) {
        continue;
      }
    } else {
      out->instance_id = info.instance_id;
      if (!in.changable_weights) {
        return kReturnOk;
      }
      const Weight::AddInflightResult r = info.weight->AddInflight(in, index, dice - left);
      if (r.weight_diff) {
        s->UpdateParentWeights(r.weight_diff, index);
        total_ += r.weight_diff;
      }
      // 若r.chosen为false，是因实例被关闭或inflight导致权重不足，重新选择
      if (r.chosen) {
        out->need_feedback = true;
        return kReturnOk;
      }
      // 尝试次数超过了实例数
      if (++ntry >= n) {
        break;
      }
    }
    total           = total_;
    dice_proportion = static_cast<double>(rand_r(&thread_local_seed)) / RAND_MAX;
    dice            = total * dice_proportion;
    index           = 0;
  }
  ++select_failed_times_;
  return kReturnUnknownError;
}

void LocalityAwareSelector::Feedback(const CallInfo &info) {
  DoublyBufferedData<Instances>::ScopedPtr s;
  // 上“读锁”，s析构时自动释放锁
  if (db_instances_.Read(&s) != 0) {
    return;
  }
  std::map<polaris::InstanceId, size_t>::const_iterator iter =
      s->instance_map.find(info.instance_id);
  if (iter == s->instance_map.end()) {
    // 实例不存在
    return;
  }
  const size_t index = iter->second;
  Weight *w          = s->weight_tree[index].weight;
  const int64_t diff = w->Update(info, index);  // 更新实例权重
  if (diff != 0) {
    s->UpdateParentWeights(diff, index);  // 更新父节点left值
    total_ += diff;                       // 更新权重总和
  }
}

int64_t LocalityAwareSelector::Weight::Update(const CallInfo &ci, size_t index) {
  // 实例节点的锁，这把锁放获取end_time_us的后面，end_time_us会更准
  // 放前面，可以防止获取end_time_us后，其他线程往time_q_里加数据
  sync::MutexGuard lock(mutex_);

  // 这个end_time不是业务报进来的，但影响不大，用来快速计算QPS
  const int64_t end_time_us = Time::GetCurrentTimeUs();
  const int64_t latency     = ci.call_daley;

  if (Disabled()) {
    // 该节点即将被删除，不对其进行Update
    return 0;
  }
  // 用于跟踪未返回调用的耗时，时间太长时进行处罚
  begin_time_sum_ -= ci.begin_time_us;
  --begin_time_count_;

  if (latency <= 0) {
    // 错误的延时
    return 0;
  }

  // Add a new entry
  TimeInfo tm_info;
  tm_info.latency_sum = latency;
  tm_info.end_time_us = end_time_us;
  if (!time_q_.Empty()) {
    tm_info.latency_sum += time_q_.Bottom()->latency_sum;
  }
  time_q_.ElimPush(tm_info);

  const int64_t top_time_us = time_q_.Top()->end_time_us;
  const size_t n            = time_q_.Size();
  int64_t scaled_qps        = kDefaultQPS * kWeightScale;
  if (end_time_us > top_time_us) {
    // Only calculate scaled_qps when the queue is full or the elapse
    // between bottom and top is reasonably large(so that error of the
    // calculated QPS is probably smaller).
    if (n == time_q_.Capacity() || end_time_us >= top_time_us + 1000000L /*1s*/) {
      // will not overflow.
      scaled_qps = (n - 1) * 1000000L * kWeightScale / (end_time_us - top_time_us);
      if (scaled_qps < kWeightScale) {
        scaled_qps = kWeightScale;
      }
    }
    avg_latency_ = (time_q_.Bottom()->latency_sum - time_q_.Top()->latency_sum) / (n - 1);
  } else if (n == 1) {
    avg_latency_ = time_q_.Bottom()->latency_sum;
  } else {
    // end_time_us <= top_time_us && n > 1: the QPS is so high that
    // the time elapse between top and bottom is 0(possible in examples),
    // or time skews, we don't update the weight for safety.
    return 0;
  }
  if (avg_latency_ == 0) {
    return 0;
  }
  base_weight_ = scaled_qps / avg_latency_;
  return ResetWeight(index, end_time_us);
}

void LocalityAwareSelector::Destroy() { delete this; }

void LocalityAwareSelector::Weight::Describe(std::ostream &os, int64_t now) {
  mutex_.Lock();
  int64_t begin_time_sum = begin_time_sum_;
  int begin_time_count   = begin_time_count_;
  int64_t weight         = weight_;
  int64_t base_weight    = base_weight_;
  size_t n               = time_q_.Size();
  double qps             = 0;
  int64_t avg_latency    = avg_latency_;
  if (n <= 1UL) {
    qps = 0;
  } else {
    if (n == time_q_.Capacity()) {
      --n;
    }
    qps = n * 1000000 / static_cast<double>(now - time_q_.Top()->end_time_us);
  }
  mutex_.Unlock();

  os << "weight=" << weight;
  if (base_weight != weight) {
    os << "(base=" << base_weight << ')';
  }
  if (begin_time_count != 0) {
    os << " inflight_delay=" << now - begin_time_sum / begin_time_count
       << "(count=" << begin_time_count << ')';
  } else {
    os << " inflight_delay=0";
  }
  os << " avg_latency=" << avg_latency << " expected_qps=" << qps;
}

void LocalityAwareSelector::Weight::Describe(std::string &str, int64_t now) {
  mutex_.Lock();
  int64_t begin_time_sum = begin_time_sum_;
  int begin_time_count   = begin_time_count_;
  int64_t weight         = weight_;
  int64_t base_weight    = base_weight_;
  size_t n               = time_q_.Size();
  double qps             = 0;
  int64_t avg_latency    = avg_latency_;
  if (n <= 1UL) {
    qps = 0;
  } else {
    if (n == time_q_.Capacity()) {
      --n;
    }
    if (now - time_q_.Top()->end_time_us > 0) {  // 此时已上锁，time_q_不会再加数据
      qps = n * 1000000 / static_cast<double>(now - time_q_.Top()->end_time_us);
    } else {
      qps = -1;
    }
  }
  mutex_.Unlock();

  str += "weight=";
  str += StringUtils::TypeToStr<int64_t>(weight);
  if (base_weight != weight) {
    str += "(base=";
    str += StringUtils::TypeToStr<int64_t>(base_weight);
    str += ')';
  }
  if (begin_time_count != 0) {
    str += " inflight_delay=";
    str += StringUtils::TypeToStr<int64_t>(now - begin_time_sum / begin_time_count);
    str += "(count=";
    str += StringUtils::TypeToStr<int>(begin_time_count);
    str += ')';
  } else {
    str += " inflight_delay=0";
  }
  str += " avg_latency=";
  str += StringUtils::TypeToStr<int64_t>(avg_latency);
  str += " expected_qps=";
  str += StringUtils::TypeToStr<double>(qps);
}

void LocalityAwareSelector::Describe(std::ostream &os) {
  os << "LocalityAware{total=" << total_ << ' ';
  DoublyBufferedData<Instances>::ScopedPtr s;
  if (db_instances_.Read(&s) != 0) {
    os << "fail to read db_instances_";
  } else {
    const int64_t now = Time::GetCurrentTimeUs();
    const size_t n    = s->weight_tree.size();
    os << '[';
    for (size_t i = 0; i < n; ++i) {
      const InstanceInfo &info = s->weight_tree[i];
      os << "\n{id=" << info.instance_id.c_str();
      os << " left=" << *info.left << ' ';
      info.weight->Describe(os, now);
      os << '}';
    }
    os << ']';
  }
  os << "}\n";
}

// 使用Polaris日志接口打印状态
void LocalityAwareSelector::Describe(std::string &str_info) {
  str_info += "LocalityAware{total=";
  str_info += StringUtils::TypeToStr<int64_t>(total_);
  str_info += " times of select instance by weighted random : ";
  str_info += StringUtils::TypeToStr<uint64_t>(select_failed_times_);
  str_info += ' ';
  DoublyBufferedData<Instances>::ScopedPtr s;
  if (db_instances_.Read(&s) != 0) {
    str_info += "fail to read db_instances_";
  } else {
    const int64_t now = Time::GetCurrentTimeUs();
    const size_t n    = s->weight_tree.size();
    str_info += '[';
    for (size_t i = 0; i < n; ++i) {
      const InstanceInfo &info = s->weight_tree[i];
      str_info += "\n{id=";
      str_info += info.instance_id.c_str();
      str_info += " left=";
      str_info += StringUtils::TypeToStr<int64_t>(*info.left);
      str_info += ' ';
      info.weight->Describe(str_info, now);
      str_info += '}';
    }
    str_info += ']';
  }
  str_info += "}\n";
}

LocalityAwareSelector::Weight::Weight(int64_t initial_weight, int64_t min_weight)
    : weight_(initial_weight), base_weight_(initial_weight), min_weight_(min_weight),
      begin_time_sum_(0), begin_time_count_(0), old_diff_sum_(0), old_index_((size_t)-1L),
      old_weight_(0), avg_latency_(0), time_q_(kRecvQueueSize) {}

LocalityAwareSelector::Weight::~Weight() {}

int64_t LocalityAwareSelector::Weight::Disable() {
  sync::MutexGuard lock(mutex_);
  const int64_t saved = weight_;
  base_weight_        = -1;
  weight_             = 0;
  return saved;
}

int64_t LocalityAwareSelector::Weight::MarkOld(size_t index) {
  sync::MutexGuard lock(mutex_);
  const int64_t saved = weight_;
  old_weight_         = saved;
  old_diff_sum_       = 0;
  old_index_          = index;
  return saved;
}

std::pair<int64_t, int64_t> LocalityAwareSelector::Weight::ClearOld() {
  sync::MutexGuard lock(mutex_);
  const int64_t old_weight = old_weight_;
  const int64_t diff       = old_diff_sum_;
  old_diff_sum_            = 0;
  old_index_               = (size_t)-1;
  old_weight_              = 0;
  return std::make_pair(old_weight, diff);
}

}  // namespace polaris