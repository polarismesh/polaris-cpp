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

#include "sync/atomic.h"
#include "utils/time_clock.h"

namespace polaris {

// 无锁多生产者单消费者队列，用于LRU回收
template <typename T>
class LruQueue {
public:
  LruQueue() {
    head_.Store(new QueueNode());
    QueueNode* front = head_.Load();
    front->next_.Store(NULL);
    tail_.Store(front);
  }

  ~LruQueue() {
    QueueNode* tail = tail_.Load();
    QueueNode* next = tail->next_.Load();
    while (next != NULL) {
      tail = next->next_;
      delete next->data_;
      delete next;
      next = tail;
    }
    delete tail_.Load();
  }

  void Enqueue(T* data) {
    QueueNode* node      = new QueueNode(data);
    QueueNode* prev_head = head_.Exchange(node);
    prev_head->next_.Store(node);
  }

  bool Dequeue(uint64_t min_time) {
    QueueNode* tail = tail_.Load();
    QueueNode* next = tail->next_.Load();
    if (next == NULL) {
      return false;
    }
    if (next->delete_time_ < min_time) {
      delete next->data_;
      next->data_ = NULL;
      tail_.Store(next);
      delete tail;
      return true;
    }
    return false;
  }

private:
  struct QueueNode {
    explicit QueueNode(T* data = NULL) : delete_time_(Time::GetCurrentTimeMs()), data_(data) {}
    uint64_t delete_time_;
    T* data_;
    sync::Atomic<QueueNode*> next_;
  };

  sync::Atomic<QueueNode*> head_;
  sync::Atomic<QueueNode*> tail_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CACHE_LRU_QUEUE_H_
