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

#ifndef POLARIS_CPP_POLARIS_CACHE_LRU_MAP_H_
#define POLARIS_CPP_POLARIS_CACHE_LRU_MAP_H_

#include <string>
#include <vector>

#include "cache/lru_queue.h"
#include "sync/atomic.h"
#include "sync/mutex.h"

namespace polaris {

uint32_t MurmurInt32(const int& key);

uint32_t MurmurString(const std::string& key);

template <typename Value>
void LruValueNoOp(Value* /*value*/) {}

template <typename Value>
void LruValueDelete(Value* value) {
  delete value;
}

template <typename Value>
void LruValueIncrementRef(Value* value) {
  value->IncrementRef();
}

template <typename Value>
void LruValueDecrementRef(Value* value) {
  value->DecrementRef();
}

/// @brief LRU机制的Hash Map
template <typename Key, typename Value>
class LruHashMap {
private:
  // Lru哈希表中的一个节点
  struct MapNode {
    uint32_t hash_;
    Key key_;
    Value* value_;         // 值指针
    MapNode* probe_next_;  // 探测链表下一个节点
    MapNode* lru_pre_;     // lru链表前一个节点
    MapNode* lru_next_;    // lru链表后一个节点
  };

  struct ProbeList {  // 探测链表信息
    ProbeList() : head_(NULL) {}
    sync::Mutex mutex_;
    MapNode* head_;
  };

  struct LruList {  // LRU链表信息，尾部追加最新节点，头部删除最旧节点
    LruList() {
      sentinel_.lru_pre_  = &sentinel_;
      sentinel_.lru_next_ = &sentinel_;
    }
    sync::Mutex mutex_;
    MapNode sentinel_;
  };

  typedef uint32_t (*HashFunc)(const Key& key);

  typedef void (*LruValueOp)(Value* value);

public:
  /// @brief 指定长度
  explicit LruHashMap(std::size_t lru_size, HashFunc hash_func = MurmurString,
                      LruValueOp allocator   = LruValueIncrementRef,
                      LruValueOp deallocator = LruValueDecrementRef);

  ~LruHashMap();

  /// @brief 根据Key获取指向Value的指针，key不存在返回NULL
  Value* Get(const Key& key);

  /// @brief 更新Key对应的Value
  /// 如果key对应的value已存在，则将旧的value加入待释放列表，内部线程会延迟一定时间释放
  /// 如果传入的value为NULL，则效果等同于调用Delete方法删除key
  void Update(const Key& key, Value* value);

  /// @brief 删除指定key，并将value加入待释放列表
  bool Delete(const Key& key);

  void GetAllValuesWithRef(std::vector<Value*>& values);

private:
  void Evict();

private:
  // lru链表相关操作
  void RemoveNode(MapNode* node);
  void AddToEnd(MapNode* node);
  void MoveToEnd(MapNode* node);
  void RemoveHead(MapNode*& node);
  void PrintLru(const char* func_name);

private:
  const std::size_t lru_size_;
  const std::size_t capacity_;
  ProbeList* table_;
  HashFunc hash_func_;
  sync::Atomic<std::size_t> size_;
  LruList lru_link_;
  LruValueOp allocator_;
  LruValueOp deallocator_;
};

template <typename Key, typename Value>
LruHashMap<Key, Value>::LruHashMap(std::size_t lru_size, HashFunc hash_func, LruValueOp allocator,
                                   LruValueOp deallocator)
    : lru_size_(lru_size), capacity_(lru_size_ + lru_size_ / 4), hash_func_(hash_func),
      allocator_(allocator), deallocator_(deallocator) {
  table_ = new ProbeList[capacity_];
}

template <typename Key, typename Value>
LruHashMap<Key, Value>::~LruHashMap() {
  // 删除探测链表
  MapNode *cur, *next;
  for (std::size_t i = 0; i < capacity_; ++i) {
    cur = table_[i].head_;
    while (cur != NULL) {
      next = cur->probe_next_;
      deallocator_(cur->value_);
      delete cur;
      cur = next;
    }
  }
  delete[] table_;
}

template <typename Key, typename Value>
Value* LruHashMap<Key, Value>::Get(const Key& key) {
  uint32_t hash         = hash_func_(key) % capacity_;
  ProbeList& probe_list = table_[hash];

  sync::MutexGuard guard(probe_list.mutex_);
  MapNode* node = probe_list.head_;
  while (node != NULL) {
    if (node->hash_ == hash && node->key_ == key) {
      MoveToEnd(node);
      allocator_(node->value_);
      return node->value_;
    }
    node = node->probe_next_;
  }
  return NULL;
}

template <typename Key, typename Value>
void LruHashMap<Key, Value>::Update(const Key& key, Value* value) {
  uint32_t hash         = hash_func_(key) % capacity_;
  ProbeList& probe_list = table_[hash];

  do {  // 插入探测链表中，需要加锁进行操作
    sync::MutexGuard guard(probe_list.mutex_);
    MapNode* node = probe_list.head_;
    while (node != NULL) {  // 加锁以后重新查询一遍是否已经存在该节点
      if (node->hash_ == hash && node->key_ == key) {  // 更新value
        deallocator_(node->value_);
        node->value_ = value;
        MoveToEnd(node);
        return;
      }
      node = node->probe_next_;
    }
    // key不存在
    MapNode* new_node     = new MapNode();
    new_node->hash_       = hash;
    new_node->key_        = key;
    new_node->value_      = value;
    new_node->probe_next_ = probe_list.head_;
    probe_list.head_      = new_node;
    AddToEnd(new_node);
    size_++;
  } while (false);
  if (size_ > lru_size_) {
    Evict();
  }
  if (size_ > lru_size_) {
    Evict();
  }
}

template <typename Key, typename Value>
void LruHashMap<Key, Value>::GetAllValuesWithRef(std::vector<Value*>& values) {
  MapNode* node;
  for (std::size_t i = 0; i < capacity_; ++i) {
    ProbeList& probe_list = table_[i];
    sync::MutexGuard guard(probe_list.mutex_);
    node = probe_list.head_;
    while (node != NULL) {
      allocator_(node->value_);
      values.push_back(node->value_);
      node = node->probe_next_;
    }
  }
}

template <typename Key, typename Value>
void LruHashMap<Key, Value>::Evict() {
  MapNode* oldest = NULL;
  RemoveHead(oldest);
  if (Delete(oldest->key_)) {
    size_--;
  }
}

template <typename Key, typename Value>
bool LruHashMap<Key, Value>::Delete(const Key& key) {
  uint32_t hash         = hash_func_(key) % capacity_;
  ProbeList& probe_list = table_[hash];

  MapNode** pre = &probe_list.head_;
  sync::MutexGuard guard(probe_list.mutex_);
  MapNode* node = probe_list.head_;
  while (node != NULL) {
    if (node->hash_ == hash && node->key_ == key) {
      *pre = node->probe_next_;
      RemoveNode(node);
      deallocator_(node->value_);
      delete node;
      return true;
    }
    pre  = &node->probe_next_;
    node = node->probe_next_;
  }
  return false;
}

template <typename Key, typename Value>
void LruHashMap<Key, Value>::RemoveNode(MapNode* node) {
  sync::MutexGuard guard(lru_link_.mutex_);
  if (node->lru_pre_ == NULL || node->lru_next_ == NULL) {
    return;  // 已经被删除了
  }
  node->lru_pre_->lru_next_ = node->lru_next_;
  node->lru_next_->lru_pre_ = node->lru_pre_;
  node->lru_pre_            = NULL;
  node->lru_next_           = NULL;
  PrintLru(__func__);
}

template <typename Key, typename Value>
void LruHashMap<Key, Value>::AddToEnd(MapNode* node) {
  MapNode* tail = &lru_link_.sentinel_;
  sync::MutexGuard guard(lru_link_.mutex_);
  node->lru_next_           = tail;
  node->lru_pre_            = tail->lru_pre_;
  tail->lru_pre_->lru_next_ = node;
  tail->lru_pre_            = node;
  PrintLru(__func__);
}

template <typename Key, typename Value>
void LruHashMap<Key, Value>::MoveToEnd(MapNode* node) {
  MapNode* tail = &lru_link_.sentinel_;
  sync::MutexGuard guard(lru_link_.mutex_);
  if (node->lru_pre_ == NULL || node->lru_next_ == NULL) {
    return;  // 已经被删除了
  }
  // 从原来的地方删除
  node->lru_pre_->lru_next_ = node->lru_next_;
  node->lru_next_->lru_pre_ = node->lru_pre_;
  // 加入尾部
  node->lru_next_           = tail;
  node->lru_pre_            = tail->lru_pre_;
  tail->lru_pre_->lru_next_ = node;
  tail->lru_pre_            = node;
  PrintLru(__func__);
}

template <typename Key, typename Value>
void LruHashMap<Key, Value>::RemoveHead(MapNode*& node) {
  MapNode* head = &lru_link_.sentinel_;
  sync::MutexGuard guard(lru_link_.mutex_);
  node                      = head->lru_next_;
  node->lru_pre_->lru_next_ = node->lru_next_;
  node->lru_next_->lru_pre_ = node->lru_pre_;
  node->lru_pre_            = NULL;
  node->lru_next_           = NULL;
  PrintLru(__func__);
}

template <typename Key, typename Value>
void LruHashMap<Key, Value>::PrintLru(const char* func_name) {
  if (func_name == NULL) {
    // printf("func name: %s\n", func_name);
    // printf("head = > tail: ");
    // MapNode* head = &lru_link_.sentinel_;
    // MapNode* node = head->lru_next_;
    // while (node != head) {
    //   printf("%d ", node->key_);
    //   node = node->lru_next_;
    // }
    // printf("\ntail => head: ");
    // MapNode* tail = &lru_link_.sentinel_;
    // node          = tail->lru_pre_;
    // while (node != head) {
    //   printf("%d ", node->key_);
    //   node = node->lru_pre_;
    // }
    // printf("\n");
  }
}

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CACHE_LRU_MAP_H_
