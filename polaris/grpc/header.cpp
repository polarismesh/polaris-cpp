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

#include "header.h"

#include <string.h>

#include "utils/string_utils.h"

namespace polaris {
namespace grpc {

#define HEADER_STR_DEFINE(NAME, VALUE)            \
  static const char k##NAME[]            = VALUE; \
  static const std::size_t k##NAME##Size = sizeof(k##NAME) - 1;

// 常用头部Key定义，其他地方直接引用
namespace HeaderKeys {
HEADER_STR_DEFINE(Scheme, ":scheme")
HEADER_STR_DEFINE(Method, ":method")
HEADER_STR_DEFINE(Host, ":authority")
HEADER_STR_DEFINE(Path, ":path")
HEADER_STR_DEFINE(TE, "te")
HEADER_STR_DEFINE(ContentType, "content-type")
HEADER_STR_DEFINE(UserAgent, "user-agent")
HEADER_STR_DEFINE(AcceptEncoding, "accept-encoding")
HEADER_STR_DEFINE(GrpcAcceptEncoding, "grpc-accept-encoding")
HEADER_STR_DEFINE(GrpcTimeout, "grpc-timeout")

// HEADER_STR_DEFINE(RequestId, "request-id")
HEADER_STR_DEFINE(HttpStatus, ":status")
HEADER_STR_DEFINE(GrpcStatus, "grpc-status")
HEADER_STR_DEFINE(GrpcMessage, "grpc-message")
HEADER_STR_DEFINE(ClientIp, "client-ip")
}  // namespace HeaderKeys

// 常用头部value定义，其他地方直接引用
namespace HeaderValues {
HEADER_STR_DEFINE(ContextType, "application/grpc")
HEADER_STR_DEFINE(UserAgent, "polaris-cpp/0.9.0")
HEADER_STR_DEFINE(GrpcAcceptEncoding, "identity,deflate,gzip")
HEADER_STR_DEFINE(AcceptEncoding, "identity,gzip")
HEADER_STR_DEFINE(TE, "trailers")
HEADER_STR_DEFINE(Scheme, "http")
HEADER_STR_DEFINE(MethodPost, "POST")
}  // namespace HeaderValues

HeaderString::HeaderString() : copy_str_(NULL), size_(0), ref_str_(copy_str_) {}

HeaderString::HeaderString(const char* data, std::size_t size)
    : copy_str_(NULL), size_(size), ref_str_(data) {}

HeaderString::~HeaderString() {
  if (copy_str_ != NULL) {
    delete[] copy_str_;
  }
}

void HeaderString::SetCopy(const char* data, std::size_t size) {
  if (copy_str_ != NULL) {
    delete[] copy_str_;
  }
  copy_str_ = new char[size];
  memcpy(copy_str_, data, size);
  size_    = size;
  ref_str_ = copy_str_;
}

void HeaderString::SetCopy(const std::string& value) { SetCopy(value.c_str(), value.size()); }

void HeaderString::SetReference(const char* data, std::size_t size) {
  if (copy_str_ != NULL) {
    delete[] copy_str_;
    copy_str_ = NULL;
  }
  size_    = size;
  ref_str_ = data;
}

bool HeaderString::Equal(const char* data, std::size_t size) {
  return size == size_ && memcmp(data, ref_str_, size_) == 0;
}

HeaderMap::~HeaderMap() {
  std::list<HeaderEntry*>::iterator it;
  for (it = reserved_headers_.begin(); it != reserved_headers_.end(); ++it) {
    delete *it;
  }
  for (it = custom_headers_.begin(); it != custom_headers_.end(); ++it) {
    delete *it;
  }
}

void HeaderMap::InitGrpcHeader(const std::string& host, const std::string& path, uint64_t timeout,
                               const std::string& client_ip) {
  // reserved keys
  AddReference(HeaderKeys::kMethod, HeaderKeys::kMethodSize, HeaderValues::kMethodPost,
               HeaderValues::kMethodPostSize);
  AddReference(HeaderKeys::kScheme, HeaderKeys::kSchemeSize, HeaderValues::kScheme,
               HeaderValues::kSchemeSize);
  AddReferenceKey(HeaderKeys::kHost, HeaderKeys::kHostSize, host);
  AddReferenceKey(HeaderKeys::kPath, HeaderKeys::kPathSize, path);

  // custom keys
  AddReference(HeaderKeys::kTE, HeaderKeys::kTESize, HeaderValues::kTE, HeaderValues::kTESize);
  if (timeout > 0) {
    std::string timeout_with_unit;
    FormatToGrpcTimeout(timeout, timeout_with_unit);  // 超时时间要加上单位
    AddReferenceKey(HeaderKeys::kGrpcTimeout, HeaderKeys::kGrpcTimeoutSize, timeout_with_unit);
  }
  AddReference(HeaderKeys::kContentType, HeaderKeys::kContentTypeSize, HeaderValues::kContextType,
               HeaderValues::kContextTypeSize);
  AddReference(HeaderKeys::kUserAgent, HeaderKeys::kUserAgentSize, HeaderValues::kUserAgent,
               HeaderValues::kUserAgentSize);
  AddReference(HeaderKeys::kGrpcAcceptEncoding, HeaderKeys::kGrpcAcceptEncodingSize,
               HeaderValues::kGrpcAcceptEncoding, HeaderValues::kGrpcAcceptEncodingSize);
  AddReference(HeaderKeys::kAcceptEncoding, HeaderKeys::kAcceptEncodingSize,
               HeaderValues::kAcceptEncoding, HeaderValues::kAcceptEncodingSize);
  AddReferenceKey(HeaderKeys::kClientIp, HeaderKeys::kClientIpSize, client_ip);
}

template <typename T>
static T* remove_const(const void* object) {
  return const_cast<T*>(reinterpret_cast<const T*>(object));
}

static void InsertHeader(std::vector<nghttp2_nv>& headers, HeaderEntry& header) {
  uint8_t flags = 0;
  if (header.GetKey().IsReference()) {
    flags |= NGHTTP2_NV_FLAG_NO_COPY_NAME;
  }
  if (header.GetValue().IsReference()) {
    flags |= NGHTTP2_NV_FLAG_NO_COPY_VALUE;
  }
  nghttp2_nv nghttp2_header;
  nghttp2_header.name     = remove_const<uint8_t>(header.GetKey().Content());
  nghttp2_header.value    = remove_const<uint8_t>(header.GetValue().Content());
  nghttp2_header.namelen  = header.GetKey().Size();
  nghttp2_header.valuelen = header.GetValue().Size();
  nghttp2_header.flags    = flags;
  headers.push_back(nghttp2_header);
}

void HeaderMap::CopyToNghttp2Header(std::vector<nghttp2_nv>& final_headers) const {
  final_headers.reserve(reserved_headers_.size() + custom_headers_.size());
  std::list<HeaderEntry*>::const_iterator it;
  for (it = reserved_headers_.begin(); it != reserved_headers_.end(); ++it) {
    InsertHeader(final_headers, *(*it));
  }
  for (it = custom_headers_.begin(); it != custom_headers_.end(); ++it) {
    InsertHeader(final_headers, *(*it));
  }
}

void HeaderMap::InsertByKey(HeaderEntry* header_entry) {
  HeaderString& key = header_entry->GetKey();
  if (key.Size() > 0 && key.Content()[0] == ':') {  // 保留头部字段
    reserved_headers_.push_back(header_entry);
  } else {
    custom_headers_.push_back(header_entry);
  }
}

void HeaderMap::AddReference(const char* key, std::size_t key_size, const char* value,
                             std::size_t value_size) {
  InsertByKey(new HeaderEntry(key, key_size, value, value_size));
}

void HeaderMap::AddReferenceKey(const char* key, std::size_t key_size, const std::string& value) {
  HeaderEntry* header_entry = new HeaderEntry(key, key_size);
  header_entry->GetValue().SetCopy(value);
  InsertByKey(header_entry);
}

uint64_t HeaderMap::ByteSize() const {
  uint64_t byte_size = 0;
  std::list<HeaderEntry*>::const_iterator it;
  for (it = reserved_headers_.begin(); it != reserved_headers_.end(); ++it) {
    byte_size += (*it)->GetKey().Size();
    byte_size += (*it)->GetValue().Size();
  }
  for (it = custom_headers_.begin(); it != custom_headers_.end(); ++it) {
    byte_size += (*it)->GetKey().Size();
    byte_size += (*it)->GetValue().Size();
  }
  return byte_size;
}

HeaderEntry* HeaderMap::Get(const char* key, std::size_t key_size) {
  std::list<HeaderEntry*>::iterator it;
  for (it = reserved_headers_.begin(); it != reserved_headers_.end(); ++it) {
    if ((*it)->GetKey().Equal(key, key_size)) {
      return *it;
    }
  }
  for (it = custom_headers_.begin(); it != custom_headers_.end(); ++it) {
    if ((*it)->GetKey().Equal(key, key_size)) {
      return *it;
    }
  }
  return NULL;
}

bool HeaderMap::GetHttp2Status(uint64_t& http2_status_code) {
  // ":status"字段必须存在且是有效的uin64_t类型数字
  HeaderEntry* header = this->Get(HeaderKeys::kHttpStatus, HeaderKeys::kHttpStatusSize);
  if (header == NULL) {
    return false;
  }
  return StringUtils::SafeStrToType(header->GetValue().ToString(), http2_status_code);
}

bool HeaderMap::GetGrpcStatus(GrpcStatusCode& grpc_statue_code) {
  HeaderEntry* grpc_status_header = this->Get(HeaderKeys::kGrpcStatus, HeaderKeys::kGrpcStatusSize);
  if (grpc_status_header == NULL || grpc_status_header->GetValue().Size() == 0) {
    return false;
  }
  uint64_t status_code;
  if (!StringUtils::SafeStrToType(grpc_status_header->GetValue().ToString(), status_code) ||
      status_code > kGrpcStatusMaximumValid) {
    return false;
  }
  grpc_statue_code = static_cast<GrpcStatusCode>(status_code);
  return true;
}

std::string HeaderMap::GetGrpcMessage() {
  HeaderEntry* header_entry = this->Get(HeaderKeys::kGrpcMessage, HeaderKeys::kGrpcMessageSize);
  return header_entry != NULL ? header_entry->GetValue().ToString() : "";
}

void HeaderMap::FormatToGrpcTimeout(uint64_t timeout, std::string& str_timeout) {
  static const char units[]                    = "mSMH";
  static const uint64_t MAX_GRPC_TIMEOUT_VALUE = 99999999;

  const char* unit = units;  // start with milliseconds
  if (timeout > MAX_GRPC_TIMEOUT_VALUE) {
    timeout /= 1000;  // Convert from milliseconds to seconds
    unit++;
  }
  while (timeout > MAX_GRPC_TIMEOUT_VALUE) {
    if (*unit == 'H') {
      timeout = MAX_GRPC_TIMEOUT_VALUE;  // No bigger unit available, clip to max 8 digit hours.
    } else {
      timeout /= 60;  // Convert from seconds to minutes to hours
      unit++;
    }
  }
  str_timeout = StringUtils::TypeToStr(timeout);
  str_timeout.append(unit, 1);
}

}  // namespace grpc
}  // namespace polaris
