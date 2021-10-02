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

#ifndef POLARIS_CPP_POLARIS_GRPC_BUFFER_H_
#define POLARIS_CPP_POLARIS_GRPC_BUFFER_H_

#include <stdint.h>
#include <string.h>

#include <deque>

#include "polaris/noncopyable.h"

namespace polaris {
namespace grpc {

// 内存块封装，包含内存起始地址和可用长度
struct RawSlice {
  explicit RawSlice(void* mem = NULL, size_t len = 0) : mem_(mem), len_(len) {}

  void* mem_;
  size_t len_;

  bool operator==(const RawSlice& rhs) const { return mem_ == rhs.mem_ && len_ == rhs.len_; }
};

/**
 * A Slice manages a contiguous block of bytes.
 * The block is arranged like this:
 *                             |<- DataSize() ->|<- ReservableSize() ->|
 * ----------+-----------------+----------------+----------------------+
 * Slice Self| Drained         | Data           | Reservable           |
 * data      | Unused space    | Usable content | New content can be   |
 * reservable| that formerly   |                | added here with      |
 * capcity   | was in the Data |                | Reserve()/Commit()   |
 * base      | section         |                |                      |
 * ----------+-----------------+----------------+----------------------+
 *           ^                 ^
 *          base              Data()
 */
class Slice : public Noncopyable {
private:  // 屏蔽构造和析构函数，只能通过Create和Release方法创建和释放
  Slice(uint64_t data, uint64_t reservable, uint64_t capacity, uint8_t* base)
      : data_(data), reservable_(reservable), capacity_(capacity), base_(base) {}
  ~Slice() {}

public:
  // Data方法返回指针指向可用内容的开始位置
  uint8_t* Data() { return base_ + data_; }
  const uint8_t* Data() const { return base_ + data_; }

  // 可用内容的长度
  uint64_t DataSize() const { return reservable_ - data_; }

  // 移除可用内容区域指定长度的数据
  void Drain(uint64_t size);

  // 返回保留区域长度
  uint64_t ReservableSize() const;

  // 从保留区域取指定长度的内存块，对内存块写入数据后需要调用Commit方法提交
  // 如果保留区长度小于size，则返回的size为保留区长度
  // 未提交的内存块可以被重新取出后写入覆盖之前的内容
  RawSlice Reserve(uint64_t size);

  // 提交从Reserve分配出去的内存块，提供成功后reservation的数据会添加到可用区域
  // 如果实际使用的内存比分配的内存少，则修改分配内存长度为使用内存长度再提交
  bool Commit(const RawSlice& reservation);

  // 拷贝尽量多的数据到Slice中，返回拷贝的数据大小
  uint64_t Append(const void* data, uint64_t size);

  // 创建指定容量的Slice
  static Slice* Create(uint64_t capacity);

  // 创建size长度的Slice并复制data内容到该Slice
  static Slice* Create(const void* data, uint64_t size);

  // 释放Slice，会释放自身内存，所以不需要调用析构函数
  void Release();

private:
  // 计算用于存放指定长度的数据需要分配的Slice长度
  static uint64_t SliceSize(uint64_t data_size);

private:
  uint64_t data_;        // 从Slice开始位置到数据区域的偏移
  uint64_t reservable_;  // 从Slice开始位置到保留区域的偏移
  uint64_t capacity_;    // Slice的容量
  uint8_t* base_;        // Slice开始位置
};

class Buffer {
public:
  Buffer() : length_(0) {}
  ~Buffer();

  // 复制数据添加Buffer中
  void Add(const void* data, uint64_t size);

  // 从Buffer中获取指定大小保留空间，并拷贝到raw_slices中
  // 返回实际使用raw_slices中的数量
  uint64_t Reserve(uint64_t length, RawSlice* raw_slices, uint64_t raw_slices_size);

  // 提交通过Reserve获取的保留空间
  void Commit(RawSlice* raw_slices, uint64_t raw_slices_size);

  // 消耗指定大小的数据
  void Drain(uint64_t size);

  // 获取内部存储的RawSlice
  // out size标示需要获取多个RawSlice，out为数组用于存储获取的RawSlice
  // 返回实际RawSlice的数量，可能大于out size
  // 如果传入NULL和0，则直接返回实际的RawSlice数据
  uint64_t GetRawSlices(RawSlice* out, uint64_t out_size);

  uint64_t Length() const;  // 返回包含的数据总长度

  // 将另一个Buffer的全部数据移动到当前Buffer末尾
  void Move(Buffer& other);

  // 移动另一个Buffer的指定长度数据到本Buffer的末尾
  void Move(Buffer& other, uint64_t length);

  // 返回值大于等于0表示读出字节大写，小于0表示出错码
  int Read(int fd, uint64_t max_length);

  // 返回值大于等于0表示写入字节大写，小于0表示出错码
  int Write(int fd);

private:
  std::deque<Slice*> slices_;  // slice构成的ring buffer
  uint64_t length_;            // 所有slice中数据总长度
};

}  // namespace grpc
}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_GRPC_BUFFER_H_
