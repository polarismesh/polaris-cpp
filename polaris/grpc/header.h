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

#ifndef POLARIS_CPP_POLARIS_GRPC_HEADER_H_
#define POLARIS_CPP_POLARIS_GRPC_HEADER_H_

#include <nghttp2/nghttp2.h>
#include <stddef.h>
#include <stdint.h>

#include <list>
#include <string>
#include <vector>

#include "grpc/status.h"
#include "polaris/noncopyable.h"

namespace polaris {
namespace grpc {

class HeaderString : public polaris::Noncopyable {
public:
  HeaderString();
  explicit HeaderString(const char* data, std::size_t size);

  ~HeaderString();

  void SetCopy(const char* data, std::size_t size);
  void SetCopy(const std::string& value);
  void SetReference(const char* data, std::size_t size);

  const char* Content() { return ref_str_; }

  bool IsReference() { return copy_str_ == NULL; }

  std::size_t Size() const { return size_; }

  std::string ToString() { return std::string(ref_str_, size_); }

  bool Equal(const char* data, std::size_t size);

private:
  char* copy_str_;       // 引用时copy_str_为NULL
  std::size_t size_;     // 数据长度
  const char* ref_str_;  // 引用外部数据或指向copy_str_
};

class HeaderEntry {
public:
  HeaderEntry() {}
  explicit HeaderEntry(const char* key, std::size_t size) : key_(key, size) {}
  HeaderEntry(const char* key, std::size_t key_size, const char* value, std::size_t value_size)
      : key_(key, key_size), value_(value, value_size) {}
  ~HeaderEntry() {}

  HeaderString& GetKey() { return key_; }
  HeaderString& GetValue() { return value_; }

private:
  HeaderString key_;
  HeaderString value_;
};

class HeaderMap {
public:
  HeaderMap() {}
  ~HeaderMap();

  void InitGrpcHeader(const std::string& authority, const std::string& path, uint64_t timeout,
                      const std::string& client_ip);

  void CopyToNghttp2Header(std::vector<nghttp2_nv>& final_headers) const;

  void InsertByKey(HeaderEntry* header_entry);

  uint64_t ByteSize() const;

  // 获取http2 status code
  bool GetHttp2Status(uint64_t& http2_status_code);

  // 获取grpc status code
  bool GetGrpcStatus(GrpcStatusCode& grpc_statue_code);

  // 获取grpc message
  std::string GetGrpcMessage();

  // 将时间戳格式化成grpc的时间格式
  static void FormatToGrpcTimeout(uint64_t timeout, std::string& str_timeout);

private:
  void AddReference(const char* key, std::size_t key_size, const char* value,
                    std::size_t value_size);

  void AddReferenceKey(const char* key, std::size_t key_size, const std::string& value);

  HeaderEntry* Get(const char* key, std::size_t key_size);

private:
  std::list<HeaderEntry*> reserved_headers_;
  std::list<HeaderEntry*> custom_headers_;
};

}  // namespace grpc
}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_GRPC_HEADER_H_
