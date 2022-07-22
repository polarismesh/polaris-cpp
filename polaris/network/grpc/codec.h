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

#ifndef POLARIS_CPP_POLARIS_NETWORK_GRPC_CODEC_H_
#define POLARIS_CPP_POLARIS_NETWORK_GRPC_CODEC_H_

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include <google/protobuf/message.h>

#include "network/buffer.h"

namespace polaris {
namespace grpc {

// 实现Grpc消息的编解码
// 参考https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md
class GrpcCodec {
 public:
  // 将PB序列化成Grpc格式，包括1字节压缩标记和4字节长度
  static Buffer* SerializeToGrpcFrame(const google::protobuf::Message& message);

  // 从Buffer反序列化出PB格式message，并返回序列化结果。不管是否序列化成功，buffer都会被释放
  // buffer中的数据已经去掉了1字节压缩标记和4字节长度
  static bool ParseBufferToMessage(Buffer* buffer, google::protobuf::Message& message);
};

const uint8_t GRPC_FH_DEFAULT = 0x0u;     // 表示消息未压缩
const uint8_t GRPC_FH_COMPRESSED = 0x1u;  // 表示使用Header中的Message-Encoding值进行压缩

// Length-Prefixed-Message 反序列化出5字节前缀后的数据
struct LengthPrefixedMessage {
  LengthPrefixedMessage() : flags_(0), length_(0), data_(nullptr) {}
  ~LengthPrefixedMessage() {
    if (data_ != nullptr) {
      delete data_;
      data_ = nullptr;
    }
  }

  uint8_t flags_;    // 压缩标记
  uint32_t length_;  // 长度
  Buffer* data_;     // 反序列完成压缩标记和长度后剩余用于反序列PB的数据
};

// 用于从数据中解码Grpc Length-Prefixed-Message
class GrpcDecoder {
 public:
  GrpcDecoder();

  // 从Buffer中已LengthPrefixedMessage解码Grpc消息
  // 如果压缩标记有问题，则返回false
  // 对于完整解码的消息，增加到output中
  // 对于未完整解码的部分，则保留在decoding_msg_中，调用次方法可继续解码
  bool Decode(Buffer& input, std::vector<LengthPrefixedMessage>& output);

 private:
  enum State {
    kStateFH_FLAG,   // 等待解码首字节压缩标记
    kStateFH_LEN_0,  // 等待解码第一个长度字节
    kStateFH_LEN_1,  // 等待解码第二个长度字节
    kStateFH_LEN_2,  // 等待解码第三个长度字节
    kStateFH_LEN_3,  // 等待解码第四个长度字节
    kStateDATA,      // 等待解码数据内容
  };

  void ResetDecodingMsg();  // 重置decoding_msg_

  State state_;                         // 解码状态
  LengthPrefixedMessage decoding_msg_;  // 正在解码消息
};

}  // namespace grpc
}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_NETWORK_GRPC_CODEC_H_
