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

#include "network/grpc/zero_copy_input_stream_impl.h"

#include <gtest/gtest.h>

namespace polaris {
namespace grpc {

TEST(ZeroCopyInputStreamImplTest, RawSlice) {
  for (int i = 0; i < 100; ++i) {
    Buffer* buffer = new Buffer();
    for (int j = 0; j <= i * 100; ++j) {
      buffer->Add("abcdef", 6);
    }
    int data_size = buffer->Length();
    ZeroCopyInputStreamImpl input_stream(buffer);
    const void* data = nullptr;
    int size = 0;
    int j = 0;
    while (input_stream.Next(&data, &size)) {
      ASSERT_TRUE(data != nullptr);
      ASSERT_TRUE(size > 0);
      if (++j % 2 == 0) {
        input_stream.BackUp(size / 2 + 1);
      }
    }
    ASSERT_EQ(input_stream.ByteCount(), data_size);
  }
}

}  // namespace grpc
}  // namespace polaris
