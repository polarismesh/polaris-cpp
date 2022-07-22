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

#include "network/trpc/buffer.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace polaris {

namespace trpc {

TEST(Buffer, construct_test) {
  Buffer buf(100);

  char* mem = new char[1024];

  Buffer buf_mem(mem, 1024);

  EXPECT_EQ(1024, buf_mem.ReadableSize());

  BufferPtr buf_ptr = std::make_shared<Buffer>(100);

  mem = new char[1024];

  mem[0] = 0x12;

  const char* const_mem = mem;

  auto copy_ptr = new Buffer(const_mem, 1024);

  EXPECT_EQ(copy_ptr->ReadableSize(), 1024);

  EXPECT_EQ(copy_ptr->GetWritePtr(), nullptr);

  EXPECT_EQ(copy_ptr->GetReadPtr(), mem);

  EXPECT_EQ(copy_ptr->WritableSize(), 0);

  delete copy_ptr;

  EXPECT_EQ(const_mem[0], 0x12);

  copy_ptr = new Buffer(mem, 1024);

  EXPECT_EQ(copy_ptr->GetWritePtr(), mem + 1024);

  EXPECT_EQ(copy_ptr->GetReadPtr(), mem);

  EXPECT_EQ(copy_ptr->WritableSize(), 0);

  delete copy_ptr;
}

TEST(Buffer, read_write_test) {
  Buffer buf(1024);

  uint64_t val = 0x1000100010001000;

  memcpy(buf.GetWritePtr(), &val, 8);

  buf.AddWriteLen(8);

  uint64_t tmp = 0;

  memcpy(&tmp, buf.GetReadPtr(), buf.ReadableSize());

  EXPECT_EQ(tmp, val);

  buf.AddReadLen(8);

  val = 0x100000;

  buf.Resize(16);

  EXPECT_EQ(buf.WritableSize(), 16);

  memcpy(buf.GetWritePtr(), &val, 8);

  buf.AddWriteLen(8);

  memcpy(&tmp, buf.GetReadPtr(), buf.ReadableSize());

  EXPECT_EQ(tmp, 0x100000);

  char* mem = new char[8];

  memset(mem, 0x50, 8);

  Buffer buf_mem(mem, 8);

  EXPECT_EQ(buf_mem.ReadableSize(), 8);

  memcpy(&tmp, buf_mem.GetReadPtr(), 8);

  EXPECT_EQ(0x5050505050505050, tmp);

  buf_mem.Resize(1024);

  EXPECT_TRUE(buf_mem.WritableSize() == 1024);
}

}  // namespace trpc
}  // namespace polaris
