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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_LOCALITY_AWARE_CIRCULAR_QUEUE_H_
#define POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_LOCALITY_AWARE_CIRCULAR_QUEUE_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace polaris {

// 循环队列CircularQueue，不负责元素的释放
template <typename T>
class CircularQueue {
 public:
  explicit CircularQueue(size_t queue_size) {
    items_ = new T[queue_size];
    cap_ = queue_size;
    count_ = 0;
    start_ = 0;
  }

  ~CircularQueue() {
    if (items_ != nullptr) {
      delete[] items_;
    }
  }

  // 向队列尾部追加元素，队列满时返回false
  bool Push(const T& item) {
    if (count_ < cap_) {
      T* iter = (items_ + Mod(start_ + count_, cap_));
      *iter = item;
      ++count_;
      return true;
    }
    return false;
  }

  // 如果容器未满，将元素追加到末尾
  // 否则，将首部元素弹出再追加
  void ElimPush(const T& item) {
    if (count_ < cap_) {
      T* iter = (items_ + Mod(start_ + count_, cap_));
      *iter = item;
      ++count_;
    } else {
      items_[start_] = item;
      start_ = Mod(start_ + 1, cap_);
    }
  }

  // 弹出队列首部元素，队列为空时返回false
  bool Pop() {
    if (count_) {
      --count_;
      start_ = Mod(start_ + 1, cap_);
      return true;
    }
    return false;
  }

  // 清空队列，容器不负责元素的内存释放
  void Clear() {
    count_ = 0;
    start_ = 0;
  }

  // 返回容器首部的指针，容器为空时返回NULL
  T* Top() { return count_ ? (items_ + start_) : nullptr; }

  // 返回容器尾部的指针，容器为空时返回NULL
  T* Bottom() { return count_ ? (items_ + Mod(start_ + count_ - 1, cap_)) : nullptr; }

  // 返回元素数量
  uint32_t Size() { return count_; }

  // 返回容器容量
  uint32_t Capacity() { return cap_; }

  bool Empty() const { return !count_; }

  bool Full() const { return cap_ == count_; }

 private:
  static uint32_t Mod(uint32_t off, uint32_t cap) {
    while (off >= cap) {
      off -= cap;
    }
    return off;
  }

 private:
  uint32_t count_;
  uint32_t cap_;
  uint32_t start_;
  T* items_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_LOCALITY_AWARE_CIRCULAR_QUEUE_H_