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

#include "buffer.h"

#include <stdint.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <algorithm>

namespace polaris {
namespace grpc {

void Slice::Drain(uint64_t size) {
  POLARIS_ASSERT(data_ + size <= reservable_);
  data_ += size;
  if (data_ == reservable_) {  // 所有可用内容都被移除，则重置偏移从而使Slice可以重复使用
    data_       = 0;
    reservable_ = 0;
  }
}

RawSlice Slice::Reserve(uint64_t size) {
  if (size == 0) {
    return RawSlice();
  }
  // 这里强制校验，如果可用区域长度为0，那偏移必须为0
  POLARIS_ASSERT(DataSize() > 0 || data_ == 0);
  uint64_t available_size = capacity_ - reservable_;
  if (available_size == 0) {
    return RawSlice();
  }
  uint64_t reservation_size = std::min(size, available_size);
  void* reservation         = &(base_[reservable_]);
  return RawSlice(reservation, static_cast<size_t>(reservation_size));
}

bool Slice::Commit(const RawSlice& reservation) {
  if (static_cast<const uint8_t*>(reservation.mem_) != (base_ + reservable_) ||
      reservable_ + reservation.len_ > capacity_ || reservable_ >= capacity_) {
    return false;  // 要提交的内存块，不是从本Slice分配的
  }
  reservable_ += reservation.len_;
  return true;
}

uint64_t Slice::Append(const void* data, uint64_t size) {
  uint64_t copy_size = std::min(size, ReservableSize());
  uint8_t* dest      = base_ + reservable_;
  reservable_ += copy_size;
  memcpy(dest, data, copy_size);
  return copy_size;
}

Slice* Slice::Create(uint64_t capacity) {
  uint64_t slice_capacity = SliceSize(capacity);
  uint8_t* base           = new uint8_t[sizeof(Slice) + slice_capacity];
  return new Slice(0, 0, slice_capacity, base + sizeof(Slice));
}

Slice* Slice::Create(const void* data, uint64_t size) {
  uint64_t slice_capacity = SliceSize(size);
  uint8_t* base           = new uint8_t[sizeof(Slice) + slice_capacity];
  Slice* slice            = new Slice(0, 0, slice_capacity, base + sizeof(Slice));
  memcpy(slice->base_, data, size);
  slice->reservable_ = size;
  return slice;
}

void Slice::Release() {
  uint8_t* base = base_ - sizeof(Slice);
  delete[] base;
  delete this;
}

uint64_t Slice::SliceSize(uint64_t data_size) {
  static const uint64_t PageSize = 4096;
  const uint64_t num_pages       = (sizeof(Slice) + data_size + PageSize - 1) / PageSize;
  return num_pages * PageSize - sizeof(Slice);
}

///////////////////////////////////////////////////////////////////////////////
Buffer::~Buffer() {
  for (std::deque<Slice*>::iterator it = slices_.begin(); it != slices_.end(); ++it) {
    (*it)->Release();
  }
}

void Buffer::Add(const void* data, uint64_t size) {
  const char* src       = static_cast<const char*>(data);
  bool new_slice_needed = slices_.empty();
  while (size != 0) {
    if (new_slice_needed) {
      Slice* slice = Slice::Create(size);
      slices_.push_back(slice);
    }
    uint64_t copy_size = slices_.back()->Append(src, size);
    src += copy_size;
    size -= copy_size;
    length_ += copy_size;
    new_slice_needed = true;
  }
}

uint64_t Buffer::Reserve(uint64_t length, RawSlice* raw_slices, uint64_t raw_slices_size) {
  if (raw_slices_size == 0 || length == 0) {
    return 0;
  }
  // Check whether there are any empty slices with reservable space at the end of the buffer.
  size_t first_reservable_slice = slices_.size();
  while (first_reservable_slice > 0) {
    if (slices_[first_reservable_slice - 1]->ReservableSize() == 0) {
      break;
    }
    first_reservable_slice--;
    if (slices_[first_reservable_slice]->DataSize() != 0) {
      // There is some content in this slice, so anything in front of it is nonreservable.
      break;
    }
  }

  // Having found the sequence of reservable slices at the back of the buffer, reserve
  // as much space as possible from each one.
  uint64_t num_slices_used = 0;
  uint64_t bytes_remaining = length;
  size_t slice_index       = first_reservable_slice;
  while (slice_index < slices_.size() && bytes_remaining != 0 &&
         num_slices_used < raw_slices_size) {
    Slice*& slice                   = slices_[slice_index];
    const uint64_t reservation_size = std::min(slice->ReservableSize(), bytes_remaining);
    if (num_slices_used + 1 == raw_slices_size && reservation_size < bytes_remaining) {
      // There is only one iovec left, and this next slice does not have enough space to
      // complete the reservation. Stop iterating, with last one iovec still unpopulated,
      // so the code following this loop can allocate a new slice to hold the rest of the
      // reservation.
      break;
    }
    raw_slices[num_slices_used] = slice->Reserve(reservation_size);
    bytes_remaining -= raw_slices[num_slices_used].len_;
    num_slices_used++;
    slice_index++;
  }

  // If needed, allocate one more slice at the end to provide the remainder of the reservation.
  if (bytes_remaining != 0) {
    Slice* slice = Slice::Create(bytes_remaining);
    slices_.push_back(slice);
    raw_slices[num_slices_used] = slices_.back()->Reserve(bytes_remaining);
    bytes_remaining -= raw_slices[num_slices_used].len_;
    num_slices_used++;
  }

  POLARIS_ASSERT(num_slices_used <= raw_slices_size);
  POLARIS_ASSERT(bytes_remaining == 0);
  return num_slices_used;
}

void Buffer::Commit(RawSlice* raw_slices, uint64_t raw_slices_size) {
  if (raw_slices_size == 0) {
    return;
  }
  // Find the slices in the buffer that correspond to the raw_slices:
  // First, scan backward from the end of the buffer to find the last slice containing
  // any content. Reservations are made from the end of the buffer, and out-of-order commits
  // aren't supported, so any slices before this point cannot match the raw_slices being committed.
  ssize_t slice_index = static_cast<ssize_t>(slices_.size()) - 1;
  while (slice_index >= 0 && slices_[slice_index]->DataSize() == 0) {
    slice_index--;
  }
  if (slice_index < 0) {
    // There was no slice containing any data, so rewind the iterator at the first slice.
    slice_index = 0;
    if (!slices_[0]) {
      return;
    }
  }

  // Next, scan forward and attempt to match the slices against raw_slices.
  uint64_t num_slices_committed = 0;
  while (num_slices_committed < raw_slices_size) {
    if (slices_[slice_index]->Commit(raw_slices[num_slices_committed])) {
      length_ += raw_slices[num_slices_committed].len_;
      num_slices_committed++;
    }
    slice_index++;
    if (slice_index == static_cast<ssize_t>(slices_.size())) {
      break;
    }
  }
  POLARIS_ASSERT(num_slices_committed > 0);
}

void Buffer::Drain(uint64_t size) {
  while (size != 0) {
    if (slices_.empty()) {
      break;
    }
    Slice* slice        = slices_.front();
    uint64_t slice_size = slice->DataSize();
    if (slice_size <= size) {
      slices_.pop_front();
      length_ -= slice_size;
      size -= slice_size;
      slice->Release();
    } else {
      slices_.front()->Drain(size);
      length_ -= size;
      size = 0;
    }
  }
}

uint64_t Buffer::GetRawSlices(RawSlice* out, uint64_t out_size) {
  uint64_t num_slices = 0;
  for (size_t i = 0; i < slices_.size(); ++i) {
    Slice* slice = slices_[i];
    if (slice->DataSize() == 0) {
      continue;
    }
    if (num_slices < out_size) {
      out[num_slices].mem_ = slice->Data();
      out[num_slices].len_ = slice->DataSize();
    }
    // we need to return the total number of slices needed to access all the data in the buffer,
    // which can be larger than out_size. So we keep iterating and counting non-empty slices here,
    // even if all the caller-supplied slices have been filled.
    num_slices++;
  }
  return num_slices;
}

uint64_t Buffer::Length() const {
#ifndef NDEBUG
  // When running in debug mode, verify that the precomputed length matches the sum
  // of the lengths of the slices.
  uint64_t length = 0;
  for (std::deque<Slice*>::const_iterator it = slices_.begin(); it != slices_.end(); ++it) {
    length += (*it)->DataSize();
  }
  POLARIS_ASSERT(length == length_);
#endif
  return length_;
}

void Buffer::Move(Buffer& other) {
  POLARIS_ASSERT(&other != this);
  while (!other.slices_.empty()) {
    const uint64_t slice_size = other.slices_.front()->DataSize();
    slices_.push_back(other.slices_.front());
    other.slices_.pop_front();
    length_ += slice_size;
    other.length_ -= slice_size;
  }
}

void Buffer::Move(Buffer& other, uint64_t length) {
  POLARIS_ASSERT(&other != this);
  while (length != 0 && !other.slices_.empty()) {
    const uint64_t slice_size = other.slices_.front()->DataSize();
    const uint64_t copy_size  = std::min(slice_size, length);
    if (copy_size == 0) {
      Slice* empty_slice = other.slices_.front();
      other.slices_.pop_front();
      empty_slice->Release();
    } else if (copy_size < slice_size) {
      Add(other.slices_.front()->Data(), copy_size);
      other.slices_.front()->Drain(copy_size);
      other.length_ -= copy_size;
    } else {
      slices_.push_back(other.slices_.front());
      other.slices_.pop_front();
      length_ += slice_size;
      other.length_ -= slice_size;
    }
    length -= copy_size;
  }
}

static int SocketReadv(int fd, uint64_t max_length, RawSlice* slices, uint64_t num_slice) {
  iovec* iov                  = new iovec[num_slice];
  uint64_t num_slices_to_read = 0;
  uint64_t num_bytes_to_read  = 0;
  for (; num_slices_to_read < num_slice && num_bytes_to_read < max_length; num_slices_to_read++) {
    iov[num_slices_to_read].iov_base = slices[num_slices_to_read].mem_;
    const size_t slice_length        = std::min(slices[num_slices_to_read].len_,
                                         static_cast<size_t>(max_length - num_bytes_to_read));
    iov[num_slices_to_read].iov_len  = slice_length;
    num_bytes_to_read += slice_length;
  }
  POLARIS_ASSERT(num_bytes_to_read <= max_length);
  int result = readv(fd, iov, static_cast<int>(num_slices_to_read));
  delete[] iov;
  return result;
}

int Buffer::Read(int fd, uint64_t max_length) {
  if (max_length == 0) {
    return 0;
  }
  const uint64_t MaxSlices = 2;
  RawSlice slices[MaxSlices];
  const uint64_t num_slices = Reserve(max_length, slices, MaxSlices);
  int result                = SocketReadv(fd, max_length, slices, num_slices);
  uint64_t bytes_to_commit  = result > 0 ? result : 0;
  POLARIS_ASSERT(bytes_to_commit <= max_length);
  for (uint64_t i = 0; i < num_slices; i++) {
    slices[i].len_ = std::min(slices[i].len_, static_cast<size_t>(bytes_to_commit));
    bytes_to_commit -= slices[i].len_;
  }
  Commit(slices, num_slices);
  return result;
}

static int SocketWritev(int fd, const RawSlice* slices, uint64_t num_slice) {
  iovec* iov                   = new iovec[num_slice];
  uint64_t num_slices_to_write = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != NULL && slices[i].len_ != 0) {
      iov[num_slices_to_write].iov_base = slices[i].mem_;
      iov[num_slices_to_write].iov_len  = slices[i].len_;
      num_slices_to_write++;
    }
  }
  if (num_slices_to_write == 0) {
    delete[] iov;
    return -1;
  }
  int result = writev(fd, iov, num_slices_to_write);
  delete[] iov;
  return result;
}

int Buffer::Write(int fd) {
  const uint64_t MaxSlices = 16;
  RawSlice slices[MaxSlices];
  const uint64_t num_slices = std::min(GetRawSlices(slices, MaxSlices), MaxSlices);
  int result                = SocketWritev(fd, slices, num_slices);
  if (result > 0) {
    this->Drain(static_cast<uint64_t>(result));
  }
  return result;
}

}  // namespace grpc
}  // namespace polaris
