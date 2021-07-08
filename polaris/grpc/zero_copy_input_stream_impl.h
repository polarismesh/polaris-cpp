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

#ifndef POLARIS_CPP_POLARIS_GRPC_ZERO_COPY_INPUT_STREAM_IMPL_H_
#define POLARIS_CPP_POLARIS_GRPC_ZERO_COPY_INPUT_STREAM_IMPL_H_

#include <stdint.h>

#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/stubs/port.h>

#include "grpc/buffer.h"
#include "logger.h"
#include "utils/scoped_ptr.h"

namespace polaris {
namespace grpc {

// 封装Buffer用于Protobuf的输入
class ZeroCopyInputStreamImpl : public google::protobuf::io::ZeroCopyInputStream {
public:
  // 从Buffer创建InputStream
  explicit ZeroCopyInputStreamImpl(Buffer* buffer);

  virtual ~ZeroCopyInputStreamImpl() {}

  // google::protobuf::io::ZeroCopyInputStream
  // 从Stream中获取一块数据，如果没有更多数据返回false
  virtual bool Next(const void** data, int* size);
  // 备份count长度数据，下次调用Next时仍然可以获取到该数据
  virtual void BackUp(int count);
  // 跳过数据，该方法不实现
  virtual bool Skip(int /*count*/) { POLARIS_ASSERT(false); }
  // 返回从对象中总共读取的数据长度
  virtual google::protobuf::int64 ByteCount() const { return byte_count_; }

private:
  ScopedPtr<Buffer> buffer_;
  uint64_t position_;
  uint64_t byte_count_;
};

}  // namespace grpc
}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_GRPC_ZERO_COPY_INPUT_STREAM_IMPL_H_
