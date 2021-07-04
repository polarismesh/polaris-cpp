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

#ifndef POLARIS_CPP_POLARIS_CACHE_RCU_MAP_H_
#define POLARIS_CPP_POLARIS_CACHE_RCU_MAP_H_

#include <list>
#include <map>
#include <set>
#include <vector>

#include "logger.h"
#include "sync/mutex.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

template <typename Value>
void ValueNoOp(Value* /*value*/) {}

template <typename Value>
void ValueIncrementRef(Value* value) {
  value->IncrementRef();
}

template <typename Value>
void ValueDecrementRef(Value* value) {
  value->DecrementRef();
}

/// @brief 双缓冲少写多读Map
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
class RcuMap {
private:
  struct MapValue {
    Value* value_;
    volatile uint64_t used_time_;
  };
  typedef std::map<Key, MapValue*> InnerMap;

  typedef void (*ValueOp)(Value* value);

  struct DeletedMap {
    InnerMap* map_;
    std::set<Key>* deleted_keys_;
    uint64_t delete_time_;
  };

public:
  explicit RcuMap(ValueOp allocator = ValueIncrementRef, ValueOp deallocator = ValueDecrementRef);

  ~RcuMap();

  /// @brief 根据Key获取指向Value的指针，key不存在返回NULL
  Value* Get(const Key& key);

  /// @brief 更新Key对应的Value
  /// 如果key对应的value已存在，则将旧的value加入待释放列表，内部线程会延迟一定时间释放
  /// 如果传入的value为NULL，则效果等同于调用Delete方法删除key
  void Update(const Key& key, Value* value);

  /// @brief 删除指定key，并将value加入待释放列表
  void Delete(const Key& key);

  /// @brief 获取可删除的数据
  void CheckGc(uint64_t min_delete_time);

  /// @brief 获取一定时间未访问的key
  void CheckExpired(uint64_t min_access_time, std::vector<Key>& keys_need_expired);

  /// @brief 获取所有Value的引用
  void GetAllValuesWithRef(std::vector<Value*>& values);

private:
  void CheckSwapInLock();

private:
  InnerMap* volatile read_map_;  // 多线程读线程安全map
  std::size_t miss_time_;        // 用于记录从dirty map中查到的次数

  sync::Mutex dirty_lock_;
  InnerMap* dirty_map_;
  std::set<Key> deleted_keys_;

  // 用于根据时间戳回收数据
  std::list<MapValue> deleted_value_list_;
  std::list<DeletedMap> deleted_map_list_;

  ValueOp allocator_;
  ValueOp deallocator_;
};

template <typename Key, typename Value>
RcuMap<Key, Value>::RcuMap(ValueOp allocator, ValueOp deallocator) {
  read_map_    = new InnerMap();
  miss_time_   = 0;
  dirty_map_   = new InnerMap();
  allocator_   = allocator;
  deallocator_ = deallocator;
}

template <typename Key, typename Value>
RcuMap<Key, Value>::~RcuMap() {
  for (typename InnerMap::iterator it = dirty_map_->begin(); it != dirty_map_->end(); ++it) {
    POLARIS_ASSERT(it->second->value_ != NULL);  // dirty map中的MapValue，其value一定不为NULL
    deallocator_(it->second->value_);
    delete it->second;
  }
  delete dirty_map_;

  for (typename std::set<Key>::iterator it = deleted_keys_.begin(); it != deleted_keys_.end();
       ++it) {
    typename InnerMap::iterator map_it = read_map_->find(*it);
    POLARIS_ASSERT(map_it != read_map_->end());
    delete map_it->second;
  }
  delete read_map_;

  while (!deleted_value_list_.empty()) {
    deallocator_(deleted_value_list_.front().value_);
    deleted_value_list_.pop_front();
  }

  while (!deleted_map_list_.empty()) {
    DeletedMap& deleted_map = deleted_map_list_.front();
    for (typename std::set<Key>::iterator it = deleted_map.deleted_keys_->begin();
         it != deleted_map.deleted_keys_->end(); ++it) {
      typename InnerMap::iterator map_it = deleted_map.map_->find(*it);
      POLARIS_ASSERT(map_it != deleted_map.map_->end());
      delete map_it->second;
    }
    delete deleted_map.deleted_keys_;
    delete deleted_map.map_;
    deleted_map_list_.pop_front();
  }
}

template <typename Key, typename Value>
Value* RcuMap<Key, Value>::Get(const Key& key) {
  // 查询read map，获取结果
  Value* read_result             = NULL;
  InnerMap* current_read         = read_map_;
  typename InnerMap::iterator it = current_read->find(key);
  if (it != current_read->end()) {  // MapValue包含的value指针在整个过程中是可能改变的
    it->second->used_time_ = Time::GetCurrentTimeMs();
    read_result            = it->second->value_;
  } else {
    // 从read map未读到数据，则加锁进行后续操作
    sync::MutexGuard mutex_guard(dirty_lock_);
    if ((it = dirty_map_->find(key)) != dirty_map_->end()) {
      it->second->used_time_ = Time::GetCurrentTimeMs();
      read_result            = it->second->value_;
      if (read_map_ == current_read) {
        miss_time_++;  // 记录read map读失败，dirty map读成功次数
      }
      // 判断miss次数是否足够导致促使dirty map交换成read map
      CheckSwapInLock();
    }
  }
  if (read_result != NULL) {
    allocator_(read_result);
  }
  return read_result;
}

template <typename Key, typename Value>
void RcuMap<Key, Value>::CheckSwapInLock() {
  if (miss_time_ < dirty_map_->size()) {
    return;
  }

  InnerMap* new_dirty_map = new InnerMap(*dirty_map_);
  DeletedMap deleted_map;
  deleted_map.map_          = read_map_;
  read_map_                 = dirty_map_;
  deleted_map.delete_time_  = Time::GetCurrentTimeMs();
  deleted_map.deleted_keys_ = new std::set<Key>();
  deleted_map.deleted_keys_->swap(deleted_keys_);
  dirty_map_ = new_dirty_map;
  deleted_map_list_.push_back(deleted_map);
  miss_time_ = 0;
}

template <typename Key, typename Value>
void RcuMap<Key, Value>::Update(const Key& key, Value* value) {
  if (value == NULL) {  // 直接调用Delete
    this->Delete(key);
    return;
  }

  // 加锁将数据写入dirty map。
  sync::MutexGuard mutex_guard(dirty_lock_);
  typename InnerMap::iterator it = dirty_map_->find(key);
  if (it != dirty_map_->end()) {  // 更新dirty map
    MapValue old_value = *(it->second);
    it->second->value_ = value;
    POLARIS_ASSERT(old_value.value_ != NULL);
    old_value.used_time_ = Time::GetCurrentTimeMs();
    deleted_value_list_.push_back(old_value);  // 旧的数据加入回收列表
  } else {                                     // 插入
    MapValue* new_value = NULL;
    // 假如dirty map中没有key，那么此时可能在read map中包含被删除的key
    // 先检查read map是否有key
    if ((it = read_map_->find(key)) != read_map_->end()) {  // 有则更新并得到该value
      new_value = it->second;
      POLARIS_ASSERT(new_value->value_ == NULL);
      new_value->used_time_ = Time::GetCurrentTimeMs();  // 插入操作设置时间
      new_value->value_     = value;
      // read map删除后又插入，相当于更新，需要删除记录去掉
      POLARIS_ASSERT(deleted_keys_.find(key) != deleted_keys_.end());
      deleted_keys_.erase(key);
    } else {  // read map没有相同的key，则创建该value
      new_value             = new MapValue();
      new_value->used_time_ = Time::GetCurrentTimeMs();  // 插入操作设置时间
      new_value->value_     = value;
    }
    (*dirty_map_)[key] = new_value;
  }
}

template <typename Key, typename Value>
void RcuMap<Key, Value>::Delete(const Key& key) {
  sync::MutexGuard mutex_guard(dirty_lock_);
  typename InnerMap::iterator it = dirty_map_->find(key);
  // dirty map中没有的话，read map即使有value也已经释放成了NULL，退出即可
  if (it == dirty_map_->end()) {
    return;
  }

  // dirty map中有该key的数据，从dirty map删除，并检查read map
  MapValue* map_value = it->second;
  POLARIS_ASSERT(map_value != NULL);
  POLARIS_ASSERT(map_value->value_ != NULL);
  dirty_map_->erase(it);
  // 被删除的数据放入GC
  map_value->used_time_ = Time::GetCurrentTimeMs();
  deleted_value_list_.push_back(*map_value);
  // 重置read map中的value为NULL，不删除value
  if ((it = read_map_->find(key)) != read_map_->end()) {
    it->second->value_ = NULL;
    deleted_keys_.insert(key);
  } else {  // 只有dirty map中有，则删除value
    delete map_value;
  }
}

template <typename Key, typename Value>
void RcuMap<Key, Value>::CheckGc(uint64_t min_delete_time) {
  std::vector<Value*> values_need_delete;
  do {  // 加锁获取需要删除的values
    sync::MutexGuard mutex_guard(dirty_lock_);
    while (!deleted_value_list_.empty()) {
      MapValue& oldest_value = deleted_value_list_.front();
      if (oldest_value.used_time_ >= min_delete_time) {
        break;
      }
      values_need_delete.push_back(oldest_value.value_);
      deleted_value_list_.pop_front();
    }
  } while (false);
  for (std::size_t i = 0; i < values_need_delete.size(); ++i) {
    deallocator_(values_need_delete[i]);
  }

  std::vector<DeletedMap> map_need_delete;
  do {  // 加锁获取需要删除的map
    sync::MutexGuard mutex_guard(dirty_lock_);
    while (!deleted_map_list_.empty()) {
      DeletedMap& oldest_value = deleted_map_list_.front();
      if (oldest_value.delete_time_ >= min_delete_time) {
        break;
      }
      map_need_delete.push_back(oldest_value);
      deleted_map_list_.pop_front();
    }
  } while (false);
  for (std::size_t i = 0; i < map_need_delete.size(); ++i) {
    DeletedMap& deleted_map = map_need_delete[i];
    for (typename std::set<Key>::iterator it = deleted_map.deleted_keys_->begin();
         it != deleted_map.deleted_keys_->end(); ++it) {
      typename InnerMap::iterator map_it = deleted_map.map_->find(*it);
      POLARIS_ASSERT(map_it != deleted_map.map_->end());
      delete map_it->second;
    }
    delete deleted_map.deleted_keys_;
    delete deleted_map.map_;
  }
}

template <typename Key, typename Value>
void RcuMap<Key, Value>::CheckExpired(uint64_t min_access_time,
                                      std::vector<Key>& keys_need_expired) {
  sync::MutexGuard mutex_guard(dirty_lock_);
  for (typename InnerMap::iterator it = dirty_map_->begin(); it != dirty_map_->end(); ++it) {
    if (it->second->used_time_ <= min_access_time) {
      keys_need_expired.push_back(it->first);
    }
  }
}

template <typename Key, typename Value>
void RcuMap<Key, Value>::GetAllValuesWithRef(std::vector<Value*>& values) {
  sync::MutexGuard mutex_guard(dirty_lock_);
  for (typename InnerMap::iterator it = dirty_map_->begin(); it != dirty_map_->end(); ++it) {
    allocator_(it->second->value_);
    values.push_back(it->second->value_);
  }
}

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CACHE_RCU_MAP_H_
