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

#include "grpc/codec.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>

#include "mock/fake_server_response.h"

namespace polaris {
namespace grpc {

TEST(GrpcCodecTest, EncodeAndDecode) {
  v1::DiscoverResponse response;
  ServiceKey service_key = {"Test", "hello.world"};
  FakeServer::CreateServiceInstances(response, service_key, 100);
  Buffer* buffer = GrpcCodec::SerializeToGrpcFrame(response);
  ASSERT_TRUE(buffer != NULL);

  GrpcDecoder decoder;
  std::vector<LengthPrefixedMessage> decode_result;
  ASSERT_TRUE(decoder.Decode(*buffer, decode_result));
  delete buffer;
  ASSERT_EQ(decode_result.size(), 1);
  LengthPrefixedMessage& prefixed_message = decode_result[0];
  ASSERT_EQ(prefixed_message.flags_, 0);
  ASSERT_EQ(prefixed_message.length_, prefixed_message.data_->Length());

  v1::DiscoverResponse decode_response;
  ASSERT_TRUE(GrpcCodec::ParseBufferToMessage(prefixed_message.data_, decode_response));
  prefixed_message.data_ = NULL;
  ASSERT_EQ(decode_response.service().namespace_().value(),
            response.service().namespace_().value());
  ASSERT_EQ(decode_response.service().name().value(), response.service().name().value());
  ASSERT_EQ(decode_response.instances_size(), response.instances_size());
}

uint8_t* BufferSetLength(Buffer& buffer, int len) {
  RawSlice iovec;
  buffer.Reserve(len, &iovec, 1);
  iovec.len_       = len;
  uint8_t* current = reinterpret_cast<uint8_t*>(iovec.mem_);
  buffer.Commit(&iovec, 1);
  return current;
}

TEST(GrpcCodecTest, TestErrorFlag) {
  Buffer buffer;
  uint8_t* current = BufferSetLength(buffer, 1);
  std::vector<LengthPrefixedMessage> output;
  const uint8_t kUint8Max = 0xffu;
  for (uint8_t i = 1; i < (uint8_t)2; i++) {  // 压缩标记正确
    *current = i;
    GrpcDecoder decoder;
    ASSERT_TRUE(decoder.Decode(buffer, output));
  }
  for (uint8_t i = 2; i < kUint8Max; i++) {  // 压缩标记错误
    *current = i;
    GrpcDecoder decoder;
    ASSERT_FALSE(decoder.Decode(buffer, output));
  }
}

TEST(GrpcCodecTest, TestNotAllMessage) {
  for (int i = 0; i < 20; ++i) {
    Buffer buffer;
    uint8_t* current            = BufferSetLength(buffer, 5 + i / 2);
    *current++                  = GRPC_FH_DEFAULT;  // flags
    const uint32_t network_size = htonl(i);
    memcpy(current, reinterpret_cast<const void*>(&network_size), sizeof(uint32_t));
    GrpcDecoder decoder;
    std::vector<LengthPrefixedMessage> output;
    ASSERT_TRUE(decoder.Decode(buffer, output));
    ASSERT_EQ(output.size(), i == 0 ? 1 : 0);  // i为0时，message长度为0，足够解析
  }
}

}  // namespace grpc
}  // namespace polaris
