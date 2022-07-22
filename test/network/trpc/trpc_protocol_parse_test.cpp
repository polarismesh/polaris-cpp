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

#include "network/trpc/trpc_protocol_parse.h"

#include <gtest/gtest.h>

#include "network/trpc/trpc_protocol.h"

namespace polaris {

namespace trpc {

size_t FillTrpcRequestProtocolData(TrpcRequestProtocol* req) {
  req->fixed_header_.magic_value = ::v1::TrpcMagic::TRPC_MAGIC_VALUE;
  req->fixed_header_.data_frame_type = 0;
  req->fixed_header_.data_frame_type = 0;
  req->fixed_header_.stream_frame_type = 0;
  req->fixed_header_.data_frame_size = 0;
  req->fixed_header_.pb_header_size = 0;
  req->fixed_header_.stream_id = 0;

  req->req_header_.set_version(0);
  req->req_header_.set_call_type(0);
  req->req_header_.set_request_id(1);
  req->req_header_.set_timeout(1000);
  req->req_header_.set_caller("test_client");
  req->req_header_.set_callee("trpc.test.helloworld.Greeter");
  req->req_header_.set_func("/trpc.test.helloworld.Greeter/SayHello");

  req->SetKVInfo("key1", "value1");
  req->SetKVInfo("key2", "value2");

  uint32_t req_header_size = req->req_header_.ByteSizeLong();

  req->fixed_header_.pb_header_size = req_header_size;

  std::string body_str("hello world");

  size_t encode_buff_size = TrpcFixedHeader::TRPC_PROTO_PREFIX_SPACE + req_header_size + body_str.size();

  req->fixed_header_.data_frame_size = encode_buff_size;

  req->req_body_ = body_str;

  return encode_buff_size;
}

size_t FillTrpcResponseProtocolData(TrpcResponseProtocol* rsp) {
  rsp->fixed_header_.magic_value = ::v1::TrpcMagic::TRPC_MAGIC_VALUE;
  rsp->fixed_header_.data_frame_type = 0;
  rsp->fixed_header_.data_frame_type = 0;
  rsp->fixed_header_.stream_frame_type = 0;
  rsp->fixed_header_.data_frame_size = 0;
  rsp->fixed_header_.pb_header_size = 0;
  rsp->fixed_header_.stream_id = 0;

  rsp->rsp_header_.set_version(0);
  rsp->rsp_header_.set_call_type(0);
  rsp->rsp_header_.set_request_id(1);
  rsp->rsp_header_.set_ret(0);
  rsp->rsp_header_.set_func_ret(0);

  rsp->SetKVInfo("key1", "value1");
  rsp->SetKVInfo("key2", "value2");

  uint32_t rsp_header_size = rsp->rsp_header_.ByteSizeLong();

  rsp->fixed_header_.pb_header_size = rsp_header_size;

  std::string body_str("hello world");

  size_t encode_buff_size = TrpcFixedHeader::TRPC_PROTO_PREFIX_SPACE + rsp_header_size + body_str.size();

  rsp->fixed_header_.data_frame_size = encode_buff_size;

  rsp->rsp_body_ = body_str;
  return encode_buff_size;
}

TEST(TrpcProtocolParse, TrpcRequestProtocolParseTest) {
  TrpcRequestProtocol req;

  size_t encode_size = FillTrpcRequestProtocolData(&req);

  std::string buff;
  req.Encode(&buff);

  EXPECT_EQ(encode_size, Parse(buff, &req));
  TrpcRequestProtocol* null_req = nullptr;
  EXPECT_EQ(-1, Parse(buff, null_req));
  EXPECT_EQ(0, Parse("123", &req));
}

TEST(TrpcProtocolParse, TrpcResponseProtocolParseTest) {
  TrpcResponseProtocol rsp;

  size_t encode_size = FillTrpcResponseProtocolData(&rsp);

  std::string buff;
  rsp.Encode(&buff);

  EXPECT_EQ(encode_size, Parse(buff, &rsp));
  TrpcResponseProtocol* null_rsp = nullptr;
  EXPECT_EQ(-1, Parse(buff, null_rsp));
  EXPECT_EQ(0, Parse("123", &rsp));
}

}  // namespace trpc
}  // namespace polaris
