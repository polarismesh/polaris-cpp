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

#include "zero_copy_input_stream_impl.h"

namespace polaris {
namespace grpc {

ZeroCopyInputStreamImpl::ZeroCopyInputStreamImpl(Buffer* buffer)
    : buffer_(buffer), position_(0), byte_count_(0) {}

bool ZeroCopyInputStreamImpl::Next(const void** data, int* size) {
  if (position_ != 0) {
    buffer_->Drain(position_);
    position_ = 0;
  }

  RawSlice slice;
  const uint64_t num_slices = buffer_->GetRawSlices(&slice, 1);
  if (num_slices > 0 && slice.len_ > 0) {
    *data     = slice.mem_;
    *size     = slice.len_;
    position_ = slice.len_;
    byte_count_ += slice.len_;
    return true;
  }
  return false;
}

void ZeroCopyInputStreamImpl::BackUp(int count) {
  POLARIS_ASSERT(count >= 0);
  POLARIS_ASSERT(uint64_t(count) <= position_);
  // 上次调用必须为Next，且count必须小于或等于上次Next返回的大小
  position_ -= count;
  byte_count_ -= count;
}

}  // namespace grpc
}  // namespace polaris
