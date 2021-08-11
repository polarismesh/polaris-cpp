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

#include "grpc_codec.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>

#include "buffer.h"
#include "logger.h"
#include "zero_copy_input_stream_impl.h"

namespace polaris {
namespace grpc {

Buffer* GrpcCodec::SerializeToGrpcFrame(const google::protobuf::Message& message) {
  // Reserve enough space for the entire message and the 5 byte header.
  Buffer* body            = new Buffer();
  const size_t size       = message.ByteSizeLong();
  const size_t alloc_size = size + 5;
  RawSlice iovec;
  body->Reserve(alloc_size, &iovec, 1);
  POLARIS_ASSERT(iovec.len_ >= alloc_size);
  iovec.len_                  = alloc_size;
  uint8_t* current            = reinterpret_cast<uint8_t*>(iovec.mem_);
  *current++                  = GRPC_FH_DEFAULT;  // flags
  const uint32_t network_size = htonl(size);
  memcpy(current, reinterpret_cast<const void*>(&network_size), sizeof(uint32_t));
  current += sizeof(uint32_t);
  google::protobuf::io::ArrayOutputStream stream(current, size, -1);
  google::protobuf::io::CodedOutputStream codec_stream(&stream);
  message.SerializeWithCachedSizes(&codec_stream);
  body->Commit(&iovec, 1);
  return body;
}

bool GrpcCodec::ParseBufferToMessage(Buffer* buffer, google::protobuf::Message& message) {
  ZeroCopyInputStreamImpl stream(buffer);
  return message.ParseFromZeroCopyStream(&stream);
}

GrpcDecoder::GrpcDecoder() : state_(kStateFH_FLAG) {}

void GrpcDecoder::ResetDecodingMsg() {
  decoding_msg_.flags_  = 0;
  decoding_msg_.length_ = 0;
  decoding_msg_.data_   = NULL;
}

bool GrpcDecoder::Decode(Buffer& input, std::vector<LengthPrefixedMessage>& output) {
  uint64_t count   = input.GetRawSlices(NULL, 0);
  RawSlice* slices = new RawSlice[count];
  input.GetRawSlices(slices, count);
  for (uint64_t i = 0; i < count; ++i) {
    RawSlice& slice = slices[i];
    uint8_t* mem    = reinterpret_cast<uint8_t*>(slice.mem_);
    for (uint64_t j = 0; j < slice.len_;) {
      uint8_t c = *mem;
      switch (state_) {
        case kStateFH_FLAG:
          if (c & ~GRPC_FH_COMPRESSED) {  // 压缩标记不合法
            delete[] slices;
            return false;
          }
          decoding_msg_.flags_ = c;
          state_               = kStateFH_LEN_0;
          mem++;
          j++;
          break;
        case kStateFH_LEN_0:
          decoding_msg_.length_ = static_cast<uint32_t>(c) << 24;
          state_                = kStateFH_LEN_1;
          mem++;
          j++;
          break;
        case kStateFH_LEN_1:
          decoding_msg_.length_ |= static_cast<uint32_t>(c) << 16;
          state_ = kStateFH_LEN_2;
          mem++;
          j++;
          break;
        case kStateFH_LEN_2:
          decoding_msg_.length_ |= static_cast<uint32_t>(c) << 8;
          state_ = kStateFH_LEN_3;
          mem++;
          j++;
          break;
        case kStateFH_LEN_3:
          decoding_msg_.length_ |= static_cast<uint32_t>(c);
          if (decoding_msg_.length_ == 0) {
            output.push_back(decoding_msg_);
            this->ResetDecodingMsg();
            state_ = kStateFH_FLAG;
          } else {
            decoding_msg_.data_ = new Buffer();
            state_              = kStateDATA;
          }
          mem++;
          j++;
          break;
        case kStateDATA:
          uint64_t remain_in_buffer = slice.len_ - j;
          uint64_t remain_in_frame  = decoding_msg_.length_ - decoding_msg_.data_->Length();
          if (remain_in_buffer <= remain_in_frame) {
            decoding_msg_.data_->Add(mem, remain_in_buffer);
            mem += remain_in_buffer;
            j += remain_in_buffer;
          } else {
            decoding_msg_.data_->Add(mem, remain_in_frame);
            mem += remain_in_frame;
            j += remain_in_frame;
          }
          if (decoding_msg_.length_ == decoding_msg_.data_->Length()) {
            output.push_back(decoding_msg_);
            this->ResetDecodingMsg();
            state_ = kStateFH_FLAG;
          }
          break;
      }
    }
  }
  delete[] slices;
  return true;
}

}  // namespace grpc
}  // namespace polaris
