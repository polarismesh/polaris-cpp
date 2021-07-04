//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use
//  this file
//  except in compliance with the License. You may obtain a copy of the License
//  at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the
//  specific
//  language governing permissions and limitations under the License.
//

#include "grpc/buffer.h"

#include <gtest/gtest.h>
#include <pthread.h>
#include <unistd.h>

namespace polaris {
namespace grpc {

TEST(GrpcBufferTest, RawSlice) {
  RawSlice empty;
  char *data = new char[4];
  RawSlice data_slice(data);
  ASSERT_FALSE(empty == data_slice);
  RawSlice data_size_slice(data, 5);
  ASSERT_FALSE(data_slice == data_size_slice);
  RawSlice data_size_slice2(data, 5);
  ASSERT_EQ(data_size_slice, data_size_slice2);
  delete[] data;
}

TEST(GrpcBufferTest, CreateSlice) {
  Slice *slice = Slice::Create(0);
  ASSERT_TRUE(slice != NULL);
  ASSERT_EQ(slice->DataSize(), 0);
  ASSERT_GT(slice->ReservableSize(), 0);
  slice->Release();
  slice = NULL;

  const char *data   = "data";
  uint64_t data_size = 5;
  slice              = Slice::Create(data, data_size);
  ASSERT_EQ(slice->DataSize(), data_size);
  ASSERT_GE(slice->ReservableSize(), data_size);
  slice->Release();
}

TEST(GrpcBufferTest, SliceOperate) {
  Slice *slice = Slice::Create(16);
  ASSERT_TRUE(slice != NULL);
  ASSERT_EQ(slice->DataSize(), 0);
  ASSERT_GE(slice->ReservableSize(), 16);

  uint8_t *data_pos = slice->Data();
  slice->Append("ABCD", 4);
  ASSERT_EQ(data_pos, slice->Data());
  ASSERT_EQ(slice->DataSize(), 4);
  slice->Drain(3);  // 消耗3个字符
  ASSERT_NE(data_pos, slice->Data());
  ASSERT_EQ(slice->DataSize(), 1);

  RawSlice raw_slice = slice->Reserve(3);
  ASSERT_EQ(raw_slice.len_, 3);
  memcpy(raw_slice.mem_, "XYZ", 3);
  slice->Commit(raw_slice);
  ASSERT_EQ(slice->DataSize(), 4);

  slice->Drain(4);  // 消耗4个字符
  ASSERT_EQ(slice->DataSize(), 0);
  ASSERT_EQ(data_pos, slice->Data());

  slice->Release();
}

TEST(GrpcBufferTest, ReserveCommitDrain) {
  Buffer data;
  for (int i = 0; i < 5; i++) {
    RawSlice slices[2];
    const uint64_t num_slices = data.Reserve(4000, slices, 2);
    data.Commit(slices, num_slices);
  }
  uint64_t num_slices = data.GetRawSlices(NULL, 0);
  RawSlice *slices    = new RawSlice[num_slices];
  data.GetRawSlices(slices, num_slices);
  delete[] slices;
  data.Drain(data.Length());
}

TEST(GrpcBufferTest, AppendThenDrain) {
  Buffer data;
  const char *A = "A";
  for (int i = 0; i < 40960; i++) {
    data.Add(A, 1);
    ASSERT_EQ(data.Length(), i + 1);
  }
  while (data.Length() > 20480) {
    data.Drain(1);
  }
}

TEST(GrpcBufferTest, BufferMove) {
  for (int i = 0; i < 10; ++i) {
    Buffer data;
    for (int j = 0; j <= i * 100; ++j) {
      data.Add("abcdef", 6);
    }
    Buffer other;
    if (i % 2 == 0) {
      other.Move(data);
    } else {
      other.Move(data, data.Length());
    }
  }
}

struct ReadThreadPara {
  int read_fd_;
  int read_total_;
};

static void *ReadThread(void *args) {
  ReadThreadPara *para = static_cast<ReadThreadPara *>(args);
  for (int i = 0; i < 10; ++i) {
    int want_read = 0;
    for (int j = 0; j <= i * 100; ++j) {
      want_read += 6;
    }
    Buffer other;
    int read_len = other.Read(para->read_fd_, want_read);
    EXPECT_EQ(read_len, want_read);
    para->read_total_ += read_len;
  }
  return NULL;
}

TEST(GrpcBufferTest, WriteToFdThenRead) {
  int pipefd[2];
  ASSERT_EQ(pipe(pipefd), 0);
  ReadThreadPara para;
  para.read_fd_    = pipefd[0];
  para.read_total_ = 0;
  int write_total_ = 0;
  int write_fd     = pipefd[1];
  pthread_t tid;
  ASSERT_EQ(pthread_create(&tid, NULL, ReadThread, &para), 0);
  for (int i = 0; i < 10; ++i) {
    Buffer data;
    for (int j = 0; j <= i * 100; ++j) {
      data.Add("abcdef", 6);
    }
    int want_write = data.Length();
    int write_len  = data.Write(write_fd);
    ASSERT_EQ(want_write, write_len);
    write_total_ += write_len;
  }
  ASSERT_EQ(pthread_join(tid, NULL), 0);
  ASSERT_EQ(write_total_, para.read_total_);
  close(para.read_fd_);
  close(write_fd);
}

}  // namespace grpc
}  // namespace polaris
