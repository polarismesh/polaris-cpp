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

#include "network/grpc/header.h"

#include <gtest/gtest.h>

#include "utils/utils.h"

namespace polaris {
namespace grpc {

TEST(GrpcHeaderTest, HeaderString) {
  std::string data = "header_str";
  HeaderString header(data.c_str(), data.size());
  int i = -1;
  do {
    if (++i % 2 == 0) {
      ASSERT_EQ(header.Content(), data.c_str());
      ASSERT_TRUE(header.IsReference());
    } else {
      ASSERT_NE(header.Content(), data.c_str());
      ASSERT_FALSE(header.IsReference());
    }
    ASSERT_EQ(header.Size(), data.size());
    ASSERT_EQ(header.ToString(), data);
    ASSERT_TRUE(header.Equal(data.c_str(), data.size()));
    ASSERT_FALSE(header.Equal(nullptr, 0));
    ASSERT_FALSE(header.Equal("abced", 5));
    data += std::to_string(i);
    if (i % 2 == 0) {
      header.SetCopy(data);
    } else {
      header.SetReference(data.c_str(), data.size());
    }
  } while (i < 10);
}

TEST(GrpcHeaderTest, HeaderStringSetEmpty) {
  HeaderString header;
  header.SetCopy(nullptr, 0);
  ASSERT_EQ(header.Size(), 0);
  ASSERT_EQ(header.ToString(), "");
  std::string data;
  header.SetCopy(data.c_str(), data.size());
  ASSERT_EQ(header.Size(), 0);
  ASSERT_EQ(header.ToString(), "");
}

TEST(GrpcHeaderTest, InitGrpcHeader) {
  HeaderMap header_map;
  header_map.InitGrpcHeader("authority", "path", 0, "clientIp");
  ASSERT_TRUE(header_map.ByteSize() > 0);

  HeaderMap header_map2;
  header_map2.InitGrpcHeader("authority", "path", 1000, "clientIp");
  ASSERT_TRUE(header_map2.ByteSize() > header_map.ByteSize());
  std::vector<nghttp2_nv> final_headers;
  header_map2.CopyToNghttp2Header(final_headers);
  std::size_t total_size = 0;
  for (std::size_t i = 0; i < final_headers.size(); ++i) {
    nghttp2_nv& nv = final_headers[i];
    total_size += (nv.namelen + nv.valuelen);
  }
  ASSERT_EQ(header_map2.ByteSize(), total_size);
}

TEST(GrpcHeaderTest, GetHttp2Status) {
  HeaderMap header_map;
  ASSERT_EQ(header_map.ByteSize(), 0);
  uint64_t http2_status_code;
  ASSERT_FALSE(header_map.GetHttp2Status(http2_status_code));
  const char kHttpStatus[] = ":status";

  HeaderEntry* header_entry = new HeaderEntry();
  header_entry->GetKey().SetReference(kHttpStatus, sizeof(kHttpStatus) - 1);
  header_entry->GetValue().SetCopy("abc");
  header_map.InsertByKey(header_entry);
  ASSERT_FALSE(header_map.GetHttp2Status(http2_status_code));
  header_entry->GetValue().SetCopy("123");
  ASSERT_TRUE(header_map.GetHttp2Status(http2_status_code));
  ASSERT_EQ(http2_status_code, 123);
}

TEST(GrpcHeaderTest, GetGrpcStatus) {
  HeaderMap header_map;
  GrpcStatusCode grpc_status_code;
  ASSERT_FALSE(header_map.GetGrpcStatus(grpc_status_code));
  const char kGrpcStatus[] = "grpc-status";

  HeaderEntry* header_entry = new HeaderEntry();
  header_entry->GetKey().SetReference(kGrpcStatus, sizeof(kGrpcStatus) - 1);
  header_entry->GetValue().SetCopy("");
  header_map.InsertByKey(header_entry);
  ASSERT_FALSE(header_map.GetGrpcStatus(grpc_status_code));
  header_entry->GetValue().SetCopy("123");
  ASSERT_FALSE(header_map.GetGrpcStatus(grpc_status_code));
  header_entry->GetValue().SetCopy(std::to_string(kGrpcStatusOk));
  ASSERT_TRUE(header_map.GetGrpcStatus(grpc_status_code));
  ASSERT_EQ(grpc_status_code, kGrpcStatusOk);
}

TEST(GrpcHeaderTest, GetGrpcMessage) {
  HeaderMap header_map;
  ASSERT_TRUE(header_map.GetGrpcMessage().empty());
  const char kGrpcMessage[] = "grpc-message";

  HeaderEntry* header_entry = new HeaderEntry();
  header_entry->GetKey().SetReference(kGrpcMessage, sizeof(kGrpcMessage) - 1);
  header_entry->GetValue().SetCopy("message");
  header_map.InsertByKey(header_entry);
  ASSERT_EQ(header_map.GetGrpcMessage(), "message");
}

TEST(GrpcHeaderTest, FormatToGrpcTimeout) {
  std::string timeout;
  size_t max_value = 99999999;
  for (std::size_t i = 0; i < max_value; i = (i + 1) * 10) {
    HeaderMap::FormatToGrpcTimeout(i, timeout);
    ASSERT_EQ(timeout, std::to_string(i) + "m");
  }
  max_value = max_value * 10;
  HeaderMap::FormatToGrpcTimeout(max_value, timeout);
  ASSERT_EQ(timeout, std::to_string(max_value / 1000) + "S");

  max_value = max_value * 1000;
  HeaderMap::FormatToGrpcTimeout(max_value, timeout);
  ASSERT_EQ(timeout, std::to_string(max_value / 1000 / 60) + "M");

  max_value = max_value * 100;
  HeaderMap::FormatToGrpcTimeout(max_value, timeout);
  ASSERT_EQ(timeout, std::to_string(max_value / 1000 / 60 / 60) + "H");
}

}  // namespace grpc
}  // namespace polaris
