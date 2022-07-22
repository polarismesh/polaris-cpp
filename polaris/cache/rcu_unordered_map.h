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

#ifndef POLARIS_CPP_POLARIS_CACHE_RCU_UNORDERED_MAP_H_
#define POLARIS_CPP_POLARIS_CACHE_RCU_UNORDERED_MAP_H_

#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include "logger.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

/// @brief 双缓冲读多写少 unordered map
///
/// 两个map，read map提供无锁读，dirty map加锁写
/// 1. 两个map中的item(key, value)，它的value一定为不NULL，但value包含的指针可以为NULL
/// 2.1 一个item如果在read map，且value包含的指针不为NULL，那么它一定在dirty map
/// 2.2 一个item如果在read map，且value包含的指针为NULL，那么它一定不在dirty map（删除操作保证）
/// 3.1 一个item如果不在read map中，如果amended为false，则它一定不在dirty map中
/// 3.2 一个item如果不在read map中，如果amended为true，则它可能在dirty map中
/// 所以
/// 4.1 一个item在dirty map中，那么它的value包含的指针一定不为NULL（删除操作保证）
/// 4.2 一个item不在dirty map中，如果它在read map中，那么它的value包含的指针一定为NULL
template <typename Key, typename Value>
class RcuUnorderedMap {
 public:
  RcuUnorderedMap();

  ~RcuUnorderedMap();

  /// @brief 根据key获取指向value，key不存在返回null
  /// 默认更新最后访问时间，用于过期资源回收
  std::shared_ptr<Value> Get(const Key& key);

  /// @brief 根据key获取指向value，key不存在返回null
  /// 该方法在线程设置了rcu time时使用，可避免获取的值被释放
  Value* GetWithRcuTime(const Key& key);

  /// @brief 更新vey对应的value
  /// 如果key对应的value已存在，则将旧的value加入待释放列表，gc线程需要保证延迟一定时间释放
  void Update(const Key& key, const std::shared_ptr<Value>& value);

  /// @brief 更新key对应的value
  /// 如果value不存在，则使用updater创建新的value更新
  /// 如果key对应的value已存在，且Predict函数返回true时，则使用updater函数更新value
  /// 将旧的value加入待释放列表，内部线程会延迟一定时间释放
  std::shared_ptr<Value> Update(const Key& key,
                                std::function<std::shared_ptr<Value>(const std::shared_ptr<Value>&)> updater,
                                std::function<bool(const std::shared_ptr<Value>&)> predicate);

  /// @brief 添加新的key,value
  /// 如果key对应的value已存在，返回已存在的value
  /// 如果key不存在，则使用creator函数创建新的value插入
  std::shared_ptr<Value> CreateOrGet(const Key& key, std::function<std::shared_ptr<Value>()> creator);

  /// @brief 删除指定key，并将value加入待释放列表，并更新read map
  void Delete(const std::vector<Key>& keys);

  /// @brief 获取可删除的数据
  void CheckGc(uint64_t min_delete_time);

  /// @brief 获取一定时间未访问的key
  void CheckExpired(uint64_t min_access_time, std::vector<Key>& keys_need_expired);

  /// @brief 获取所有Value的引用
  void GetAllValues(std::vector<std::shared_ptr<Value>>& values);

  /// @brief 获取所有数据的引用
  void GetAllData(std::unordered_map<Key, std::shared_ptr<Value>>& data);

 private:
  void CheckSwapInLock();

 private:
  struct MapValue {
    MapValue() : value_(nullptr) {}

    ~MapValue() {
      if (value_ != nullptr) {
        delete value_;
        value_ = nullptr;
      }
    }

    // 这里使用原子指针，删除时可以只删除其中的value，而不修改map
    std::atomic<std::shared_ptr<Value>*> value_;
    std::atomic<uint64_t> used_time_;
  };

  // 使用shared_ptr存储MapValue，保证读写map持有相同的MapValue
  typedef std::unordered_map<Key, std::shared_ptr<MapValue>> InnerMap;

  struct DeletedMap {
    explicit DeletedMap(InnerMap* map) : map_(map), delete_time_(Time::GetCoarseSteadyTimeMs()) {}

    std::unique_ptr<InnerMap> map_;  // 待删除的map
    uint64_t delete_time_;
  };

  std::atomic<InnerMap*> read_map_;  // 多线程读线程安全map

  std::mutex dirty_lock_;
  std::unique_ptr<InnerMap> dirty_map_;
  std::size_t miss_time_;  // 用于记录从dirty map中查到的次数
  bool dirty_flag_;        // 标记read map是否和dirty map不一样

  // 用于根据时间戳回收数据
  // TODO 数据回收使用单独的锁
  std::multimap<uint64_t, std::shared_ptr<Value>*> deleted_value_list_;
  std::list<DeletedMap> deleted_map_list_;
};

template <typename Key, typename Value>
RcuUnorderedMap<Key, Value>::RcuUnorderedMap()
    : read_map_(new InnerMap()), dirty_map_(new InnerMap()), miss_time_(0), dirty_flag_(false) {}

template <typename Key, typename Value>
RcuUnorderedMap<Key, Value>::~RcuUnorderedMap() {
  if (read_map_ != nullptr) {
    delete read_map_;
    read_map_ = nullptr;
  }
  for (auto it : deleted_value_list_) {
    delete it.second;
  }
}

template <typename Key, typename Value>
std::shared_ptr<Value> RcuUnorderedMap<Key, Value>::Get(const Key& key) {
  // 查询read map，获取结果
  InnerMap* current_read = read_map_.load(std::memory_order_acquire);
  typename InnerMap::iterator it = current_read->find(key);
  if (it != current_read->end()) {
    it->second->used_time_ = Time::GetCoarseSteadyTimeMs();
    return *(it->second->value_.load(std::memory_order_acquire));
  }

  // 从read map未读到数据，则加锁进行后续操作
  const std::lock_guard<std::mutex> mutex_guard(dirty_lock_);
  if (!dirty_flag_) {
    return nullptr;
  }

  std::shared_ptr<Value> value;
  if ((it = dirty_map_->find(key)) != dirty_map_->end()) {
    it->second->used_time_ = Time::GetCoarseSteadyTimeMs();
    // 先获取结果，避免后续操作导致iter失效
    value = *(it->second->value_.load(std::memory_order_acquire));

    miss_time_++;       // 记录read map读失败，dirty map读成功次数
    CheckSwapInLock();  // 判断miss次数是否足够导致促使dirty map交换成read map
  }
  return value;
}

template <typename Key, typename Value>
Value* RcuUnorderedMap<Key, Value>::GetWithRcuTime(const Key& key) {
  // 查询read map，获取结果
  InnerMap* current_read = read_map_.load(std::memory_order_acquire);
  typename InnerMap::iterator it = current_read->find(key);
  if (it != current_read->end()) {
    it->second->used_time_ = Time::GetCoarseSteadyTimeMs();
    return (it->second->value_.load(std::memory_order_acquire))->get();
  }

  // 从read map未读到数据，则加锁进行后续操作
  const std::lock_guard<std::mutex> mutex_guard(dirty_lock_);
  if (!dirty_flag_) {
    return nullptr;
  }

  Value* value = nullptr;
  if ((it = dirty_map_->find(key)) != dirty_map_->end()) {
    it->second->used_time_ = Time::GetCoarseSteadyTimeMs();
    // 先获取结果，避免后续操作导致iter失效
    value = (it->second->value_.load(std::memory_order_acquire))->get();

    miss_time_++;       // 记录read map读失败，dirty map读成功次数
    CheckSwapInLock();  // 判断miss次数是否足够导致促使dirty map交换成read map
  }
  return value;
}

template <typename Key, typename Value>
void RcuUnorderedMap<Key, Value>::CheckSwapInLock() {
  if (miss_time_ < dirty_map_->size()) {
    return;
  }

  DeletedMap deleted_map(read_map_.load(std::memory_order_acquire));
  read_map_.store(dirty_map_.release(), std::memory_order_release);
  dirty_map_.reset(new InnerMap(*read_map_));
  deleted_map_list_.push_back(std::move(deleted_map));
  miss_time_ = 0;
  dirty_flag_ = false;
}

template <typename Key, typename Value>
void RcuUnorderedMap<Key, Value>::Update(const Key& key, const std::shared_ptr<Value>& value) {
  // 加锁将数据写入dirty map。
  const std::lock_guard<std::mutex> mutex_guard(dirty_lock_);
  auto it = dirty_map_->find(key);
  if (it != dirty_map_->end()) {  // 更新dirty map
    uint64_t current_time = Time::GetCoarseSteadyTimeMs();
    deleted_value_list_.insert(std::make_pair(current_time, it->second->value_.load(std::memory_order_acquire)));
    it->second->used_time_.store(current_time, std::memory_order_release);
    it->second->value_.store(new std::shared_ptr<Value>(value), std::memory_order_release);
  } else {
    // 假如dirty map中没有key，那么read map里也不会有该key
    std::shared_ptr<MapValue> new_value(new MapValue());
    new_value->used_time_ = Time::GetCoarseSteadyTimeMs();  // 插入操作设置时间
    new_value->value_.store(new std::shared_ptr<Value>(value), std::memory_order_release);
    dirty_map_->insert(std::make_pair(key, new_value));
    dirty_flag_ = true;
  }
}

template <typename Key, typename Value>
std::shared_ptr<Value> RcuUnorderedMap<Key, Value>::Update(
    const Key& key, std::function<std::shared_ptr<Value>(const std::shared_ptr<Value>&)> updater,
    std::function<bool(const std::shared_ptr<Value>&)> predicate) {
  const std::lock_guard<std::mutex> mutex_guard(dirty_lock_);
  auto it = dirty_map_->find(key);
  std::shared_ptr<Value> value;
  if (it == dirty_map_->end()) {  // 不存在时一定更新
    value = updater(nullptr);
    // 假如dirty map中没有key，那么read map里也不会有该key
    std::shared_ptr<MapValue> new_value(new MapValue());
    new_value->used_time_ = Time::GetCoarseSteadyTimeMs();  // 插入操作设置时间
    new_value->value_.store(new std::shared_ptr<Value>(value), std::memory_order_release);
    dirty_map_->insert(std::make_pair(key, new_value));
    dirty_flag_ = true;
  } else if (predicate(*(it->second->value_.load(std::memory_order_acquire)))) {
    value = updater(*(it->second->value_.load(std::memory_order_acquire)));
    uint64_t current_time = Time::GetCoarseSteadyTimeMs();
    deleted_value_list_.insert(std::make_pair(current_time, it->second->value_.load(std::memory_order_acquire)));
    it->second->used_time_.store(current_time, std::memory_order_release);
    it->second->value_.store(new std::shared_ptr<Value>(value), std::memory_order_release);
  } else {  // 存在且不用更新
    value = *(it->second->value_.load(std::memory_order_acquire));
  }
  return value;
}

template <typename Key, typename Value>
std::shared_ptr<Value> RcuUnorderedMap<Key, Value>::CreateOrGet(const Key& key,
                                                                std::function<std::shared_ptr<Value>()> creator) {
  const std::lock_guard<std::mutex> mutex_guard(dirty_lock_);
  auto it = dirty_map_->find(key);
  if (it != dirty_map_->end()) {
    return *(it->second->value_.load(std::memory_order_acquire));
  }

  // 假如dirty map中没有key，那么read map里也不会有该key
  std::shared_ptr<Value> value = creator();
  if (value == nullptr) {
    return nullptr;
  }
  std::shared_ptr<MapValue> new_value(new MapValue());
  new_value->used_time_ = Time::GetCoarseSteadyTimeMs();  // 插入操作设置时间
  new_value->value_.store(new std::shared_ptr<Value>(value), std::memory_order_release);
  dirty_map_->insert(std::make_pair(key, new_value));
  dirty_flag_ = true;
  return value;
}

template <typename Key, typename Value>
void RcuUnorderedMap<Key, Value>::Delete(const std::vector<Key>& keys) {
  if (keys.empty()) {
    return;
  }
  bool changed = false;
  const std::lock_guard<std::mutex> mutex_guard(dirty_lock_);
  for (auto key : keys) {
    auto dirty_it = dirty_map_->find(key);
    if (dirty_it != dirty_map_->end()) {
      dirty_map_->erase(dirty_it);
      changed = true;
    }
  }
  if (changed) {
    InnerMap* new_read_map = new InnerMap(*dirty_map_);
    DeletedMap deleted_map(read_map_.load(std::memory_order_acquire));
    read_map_.store(new_read_map, std::memory_order_release);
    deleted_map_list_.push_back(std::move(deleted_map));
    miss_time_ = 0;
    dirty_flag_ = false;
  }
}

template <typename Key, typename Value>
void RcuUnorderedMap<Key, Value>::CheckGc(uint64_t min_delete_time) {
  do {  // 加锁删除的values
    const std::lock_guard<std::mutex> mutex_guard(dirty_lock_);
    while (!deleted_value_list_.empty()) {
      auto deleted_it = deleted_value_list_.begin();
      if (deleted_it->first >= min_delete_time) {
        break;
      }
      delete deleted_it->second;
      deleted_value_list_.erase(deleted_it);
    }
  } while (false);

  std::vector<DeletedMap> map_need_delete;
  do {  // 加锁获取需要删除的map
    const std::lock_guard<std::mutex> mutex_guard(dirty_lock_);
    while (!deleted_map_list_.empty()) {
      DeletedMap& oldest_value = deleted_map_list_.front();
      if (oldest_value.delete_time_ >= min_delete_time) {
        break;
      }
      deleted_map_list_.pop_front();
    }
  } while (false);
}

template <typename Key, typename Value>
void RcuUnorderedMap<Key, Value>::CheckExpired(uint64_t min_access_time, std::vector<Key>& keys_need_expired) {
  const std::lock_guard<std::mutex> mutex_guard(dirty_lock_);
  for (auto it = dirty_map_->begin(); it != dirty_map_->end(); ++it) {
    if (it->second->used_time_.load(std::memory_order_acquire) <= min_access_time) {
      keys_need_expired.push_back(it->first);
    }
  }
}

template <typename Key, typename Value>
void RcuUnorderedMap<Key, Value>::GetAllValues(std::vector<std::shared_ptr<Value>>& values) {
  const std::lock_guard<std::mutex> mutex_guard(dirty_lock_);
  for (auto it = dirty_map_->begin(); it != dirty_map_->end(); ++it) {
    values.push_back(*it->second->value_);
  }
}

template <typename Key, typename Value>
void RcuUnorderedMap<Key, Value>::GetAllData(std::unordered_map<Key, std::shared_ptr<Value>>& data) {
  data.reserve(read_map_.load(std::memory_order_acquire)->size());
  const std::lock_guard<std::mutex> mutex_guard(dirty_lock_);
  for (auto it = dirty_map_->begin(); it != dirty_map_->end(); ++it) {
    data.insert(std::make_pair(it->first, *it->second->value_));
  }
}

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CACHE_RCU_UNORDERED_MAP_H_
