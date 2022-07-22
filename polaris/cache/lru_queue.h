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

#ifndef POLARIS_CPP_POLARIS_CACHE_LRU_QUEUE_H_
#define POLARIS_CPP_POLARIS_CACHE_LRU_QUEUE_H_

#include <atomic>

#include "utils/time_clock.h"

namespace polaris {

// 无锁多生产者单消费者队列，用于LRU回收
template <typename T>
class LruQueue {
 public:
  LruQueue() : head_(new QueueNode()), tail_(head_.load(std::memory_order_relaxed)) {}

  ~LruQueue() {
    QueueNode* tail = tail_.load();
    QueueNode* next = tail->next_.load();
    while (next != nullptr) {
      tail = next->next_;
      delete next->data_;
      delete next;
      next = tail;
    }
    delete tail_.load();
  }

  void Enqueue(T* data) {
    QueueNode* node = new QueueNode(data);
    QueueNode* prev_head = head_.exchange(node);
    prev_head->next_.store(node);
  }

  bool Dequeue(uint64_t min_time) {
    QueueNode* tail = tail_.load();
    QueueNode* next = tail->next_.load();
    if (next == nullptr) {
      return false;
    }
    if (next->delete_time_ < min_time) {
      delete next->data_;
      next->data_ = nullptr;
      tail_.store(next);
      delete tail;
      return true;
    }
    return false;
  }

 private:
  struct QueueNode {
    explicit QueueNode(T* data = nullptr) : delete_time_(Time::GetCoarseSteadyTimeMs()), data_(data), next_(nullptr) {}
    uint64_t delete_time_;
    T* data_;
    std::atomic<QueueNode*> next_;
  };

  std::atomic<QueueNode*> head_;
  std::atomic<QueueNode*> tail_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CACHE_LRU_QUEUE_H_
