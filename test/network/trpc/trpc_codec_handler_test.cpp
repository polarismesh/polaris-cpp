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

#include "network/trpc/trpc_codec_handler.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <list>
#include <memory>
#include <string>

#include "network/trpc/tcp_connection.h"
#include "network/trpc/trpc_protocol.h"

namespace polaris {

namespace trpc {

class MockConnection : public TcpConnection {
 public:
  explicit MockConnection(const TcpConnection::Options &options, RequestCallback &request_callback)
      : TcpConnection(options, request_callback) {}

  ~MockConnection() override {}

  void ReadHandler() override {}

  void WriteHandler() override {}

  void CloseHandler() override {}
};

class TrpcProtoCheckerTest : public ::testing::Test {
 protected:
  static void SetUpTestCase() {
    callback = new RequestCallback();
    TcpConnection::Options options;
    conn = new MockConnection(options, *callback);
    checker_ = new TrpcCodecHandler();
  }

  static void TearDownTestCase() {
    delete checker_;
    checker_ = nullptr;
    delete conn;
    conn = nullptr;
    delete callback;
    callback = nullptr;
  }

  virtual void SetUp() {
    in.clear();
    out.clear();
  }

  static std::list<BufferPtr> in;
  static std::list<BufferPtr> out;
  static RequestCallback *callback;
  static ConnectionPtr conn;
  static TrpcCodecHandler *checker_;
};

std::list<BufferPtr> TrpcProtoCheckerTest::in;
std::list<BufferPtr> TrpcProtoCheckerTest::out;
RequestCallback *TrpcProtoCheckerTest::callback;
ConnectionPtr TrpcProtoCheckerTest::conn;
TrpcCodecHandler *TrpcProtoCheckerTest::checker_;

size_t FillTrpcRequestProtocolData(TrpcRequestProtocol &req) {
  req.fixed_header_.magic_value = ::v1::TrpcMagic::TRPC_MAGIC_VALUE;
  req.fixed_header_.data_frame_type = 0;
  req.fixed_header_.data_frame_type = 0;
  // req.fixed_header_.data_frame_state = 0;
  req.fixed_header_.data_frame_size = 0;
  req.fixed_header_.pb_header_size = 0;
  req.fixed_header_.stream_id = 0;

  req.req_header_.set_version(0);
  req.req_header_.set_call_type(0);
  req.req_header_.set_request_id(1);
  req.req_header_.set_timeout(1000);
  req.req_header_.set_caller("test_client");
  req.req_header_.set_callee("trpc.test.helloworld.Greeter");
  req.req_header_.set_func("/trpc.test.helloworld.Greeter/SayHello");

  uint32_t req_header_size = req.req_header_.ByteSizeLong();

  req.fixed_header_.pb_header_size = req_header_size;

  std::string body_str("hello world");

  size_t encode_buff_size = TrpcFixedHeader::TRPC_PROTO_PREFIX_SPACE + req_header_size + body_str.size();

  req.fixed_header_.data_frame_size = encode_buff_size;

  req.req_body_ = body_str;

  return encode_buff_size;
}

TEST_F(TrpcProtoCheckerTest, TrpcProtoCheckerFullPacket) {
  TrpcRequestProtocol req;

  FillTrpcRequestProtocolData(req);

  std::string encode_out;
  EXPECT_TRUE(req.Encode(&encode_out) > 0);

  BufferPtr buff = std::make_shared<Buffer>(encode_out.size());
  memcpy(buff->GetWritePtr(), encode_out.c_str(), encode_out.size());
  buff->AddWriteLen(encode_out.size());

  in.clear();
  out.clear();

  in.push_back(buff);

  auto result = checker_->Check(conn, in, out);

  ASSERT_EQ(result, PacketChecker::PACKET_FULL);
  ASSERT_EQ(out.size(), 1);
  ASSERT_EQ(in.size(), 0);
}

TEST_F(TrpcProtoCheckerTest, TrpcProtoCheckerPacketLess1) {
  BufferPtr buff = std::make_shared<Buffer>(10);
  buff->AddWriteLen(10);

  in.clear();
  out.clear();

  in.push_back(buff);

  auto result = checker_->Check(conn, in, out);

  ASSERT_EQ(result, PacketChecker::PACKET_LESS);
  ASSERT_EQ(out.size(), 0);
  ASSERT_EQ(in.size(), 1);
}

TEST_F(TrpcProtoCheckerTest, TrpcProtoCheckerPACKETMAGICERR) {
  TrpcRequestProtocol req;

  req.req_header_.set_version(0);
  req.req_header_.set_call_type(0);
  req.req_header_.set_request_id(1);
  req.req_header_.set_timeout(1000);
  req.req_header_.set_caller("test_client");
  req.req_header_.set_callee("trpc.test.helloworld.Greeter");
  req.req_header_.set_func("/trpc.test.helloworld.Greeter/SayHello");

  uint32_t req_header_size = req.req_header_.ByteSizeLong();

  std::string body_str("hello world");

  uint32_t req_body_size = body_str.size();

  uint32_t total_size = TrpcFixedHeader::TRPC_PROTO_PREFIX_SPACE + req_header_size + req_body_size;

  BufferPtr buff(new Buffer(total_size));

  uint16_t magic_value = 0;  // req.fixed_header_.magic_value;
  magic_value = htons(magic_value);

  memcpy(buff->GetWritePtr(), &magic_value, TrpcFixedHeader::TRPC_PROTO_MAGIC_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_MAGIC_SPACE);

  memcpy(buff->GetWritePtr(), &(req.fixed_header_.data_frame_type), TrpcFixedHeader::TRPC_PROTO_DATAFRAME_TYPE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_DATAFRAME_TYPE_SPACE);

  memcpy(buff->GetWritePtr(), &(req.fixed_header_.stream_frame_type),
         TrpcFixedHeader::TRPC_PROTO_STREAMFRAME_TYPE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_STREAMFRAME_TYPE_SPACE);

  uint32_t data_frame_size = 10;  // total_size;
  data_frame_size = htonl(data_frame_size);

  memcpy(buff->GetWritePtr(), &data_frame_size, TrpcFixedHeader::TRPC_PROTO_DATAFRAME_SIZE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_DATAFRAME_SIZE_SPACE);

  uint16_t pb_header_size = req_header_size;
  pb_header_size = htons(req_header_size);

  memcpy(buff->GetWritePtr(), &pb_header_size, TrpcFixedHeader::TRPC_PROTO_HEADER_SIZE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_HEADER_SIZE_SPACE);

  uint32_t stream_id = req.fixed_header_.stream_id;
  stream_id = htons(stream_id);

  memcpy(buff->GetWritePtr(), &stream_id, TrpcFixedHeader::TRPC_PROTO_STREAM_ID_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_STREAM_ID_SPACE);

  memcpy(buff->GetWritePtr(), &(req.fixed_header_.reversed), TrpcFixedHeader::TRPC_PROTO_REVERSED_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_REVERSED_SPACE);

  req.req_header_.SerializeToArray(buff->GetWritePtr(), req_header_size);
  buff->AddWriteLen(req_header_size);

  memcpy(buff->GetWritePtr(), body_str.c_str(), body_str.size());
  buff->AddWriteLen(req_body_size);

  in.clear();
  out.clear();

  in.push_back(buff);

  auto result = checker_->Check(conn, in, out);

  ASSERT_EQ(result, PacketChecker::PACKET_ERR);
  ASSERT_EQ(out.size(), 0);
  ASSERT_EQ(in.size(), 1);
}

TEST_F(TrpcProtoCheckerTest, TrpcProtoCheckerPACKEDATAFRAMESIZETERR1) {
  TrpcRequestProtocol req;

  req.req_header_.set_version(0);
  req.req_header_.set_call_type(0);
  req.req_header_.set_request_id(1);
  req.req_header_.set_timeout(1000);
  req.req_header_.set_caller("test_client");
  req.req_header_.set_callee("trpc.test.helloworld.Greeter");
  req.req_header_.set_func("/trpc.test.helloworld.Greeter/SayHello");

  uint32_t req_header_size = req.req_header_.ByteSizeLong();

  std::string body_str("hello world");

  uint32_t req_body_size = body_str.size();

  uint32_t total_size = TrpcFixedHeader::TRPC_PROTO_PREFIX_SPACE + req_header_size + req_body_size;

  BufferPtr buff(new Buffer(total_size));

  uint16_t magic_value = req.fixed_header_.magic_value;
  magic_value = htons(magic_value);

  memcpy(buff->GetWritePtr(), &magic_value, TrpcFixedHeader::TRPC_PROTO_MAGIC_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_MAGIC_SPACE);

  memcpy(buff->GetWritePtr(), &(req.fixed_header_.data_frame_type), TrpcFixedHeader::TRPC_PROTO_DATAFRAME_TYPE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_DATAFRAME_TYPE_SPACE);

  memcpy(buff->GetWritePtr(), &(req.fixed_header_.stream_frame_type),
         TrpcFixedHeader::TRPC_PROTO_STREAMFRAME_TYPE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_STREAMFRAME_TYPE_SPACE);

  uint32_t data_frame_size = 10;  // total_size;
  data_frame_size = htonl(data_frame_size);

  memcpy(buff->GetWritePtr(), &data_frame_size, TrpcFixedHeader::TRPC_PROTO_DATAFRAME_SIZE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_DATAFRAME_SIZE_SPACE);

  uint16_t pb_header_size = req_header_size;
  pb_header_size = htons(req_header_size);

  memcpy(buff->GetWritePtr(), &pb_header_size, TrpcFixedHeader::TRPC_PROTO_HEADER_SIZE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_HEADER_SIZE_SPACE);

  uint32_t stream_id = req.fixed_header_.stream_id;
  stream_id = htons(stream_id);

  memcpy(buff->GetWritePtr(), &stream_id, TrpcFixedHeader::TRPC_PROTO_STREAM_ID_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_STREAM_ID_SPACE);

  memcpy(buff->GetWritePtr(), &(req.fixed_header_.reversed), TrpcFixedHeader::TRPC_PROTO_REVERSED_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_REVERSED_SPACE);

  req.req_header_.SerializeToArray(buff->GetWritePtr(), req_header_size);
  buff->AddWriteLen(req_header_size);

  memcpy(buff->GetWritePtr(), body_str.c_str(), body_str.size());
  buff->AddWriteLen(req_body_size);

  in.clear();
  out.clear();

  in.push_back(buff);

  auto result = checker_->Check(conn, in, out);

  ASSERT_EQ(result, PacketChecker::PACKET_ERR);
  ASSERT_EQ(out.size(), 0);
  ASSERT_EQ(in.size(), 1);
}

TEST_F(TrpcProtoCheckerTest, TrpcProtoCheckerPACKEDATAFRAMESIZETERR2) {
  TrpcRequestProtocol req;

  req.req_header_.set_version(0);
  req.req_header_.set_call_type(0);
  req.req_header_.set_request_id(1);
  req.req_header_.set_timeout(1000);
  req.req_header_.set_caller("test_client");
  req.req_header_.set_callee("trpc.test.helloworld.Greeter");
  req.req_header_.set_func("/trpc.test.helloworld.Greeter/SayHello");

  uint32_t req_header_size = req.req_header_.ByteSizeLong();

  std::string body_str("hello world");

  uint32_t req_body_size = body_str.size();

  uint32_t total_size = TrpcFixedHeader::TRPC_PROTO_PREFIX_SPACE + req_header_size + req_body_size;

  BufferPtr buff(new Buffer(total_size));

  uint16_t magic_value = req.fixed_header_.magic_value;
  magic_value = htons(magic_value);

  memcpy(buff->GetWritePtr(), &magic_value, TrpcFixedHeader::TRPC_PROTO_MAGIC_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_MAGIC_SPACE);

  memcpy(buff->GetWritePtr(), &(req.fixed_header_.data_frame_type), TrpcFixedHeader::TRPC_PROTO_DATAFRAME_TYPE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_DATAFRAME_TYPE_SPACE);

  memcpy(buff->GetWritePtr(), &(req.fixed_header_.stream_frame_type),
         TrpcFixedHeader::TRPC_PROTO_STREAMFRAME_TYPE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_STREAMFRAME_TYPE_SPACE);

  uint32_t data_frame_size = 10000;  // total_size;
  data_frame_size = htonl(data_frame_size);

  memcpy(buff->GetWritePtr(), &data_frame_size, TrpcFixedHeader::TRPC_PROTO_DATAFRAME_SIZE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_DATAFRAME_SIZE_SPACE);

  uint16_t pb_header_size = req_header_size;
  pb_header_size = htons(req_header_size);

  memcpy(buff->GetWritePtr(), &pb_header_size, TrpcFixedHeader::TRPC_PROTO_HEADER_SIZE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_HEADER_SIZE_SPACE);

  uint32_t stream_id = req.fixed_header_.stream_id;
  stream_id = htons(stream_id);

  memcpy(buff->GetWritePtr(), &stream_id, TrpcFixedHeader::TRPC_PROTO_STREAM_ID_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_STREAM_ID_SPACE);

  memcpy(buff->GetWritePtr(), &(req.fixed_header_.reversed), TrpcFixedHeader::TRPC_PROTO_REVERSED_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_REVERSED_SPACE);

  req.req_header_.SerializeToArray(buff->GetWritePtr(), req_header_size);
  buff->AddWriteLen(req_header_size);

  memcpy(buff->GetWritePtr(), body_str.c_str(), body_str.size());
  buff->AddWriteLen(req_body_size);

  in.clear();
  out.clear();

  in.push_back(buff);

  auto result = checker_->Check(conn, in, out);

  ASSERT_EQ(result, 0);
  ASSERT_EQ(out.size(), 0);
  ASSERT_EQ(in.size(), 1);
}

TEST_F(TrpcProtoCheckerTest, TrpcProtoCheckerPACKETLESS2) {
  TrpcRequestProtocol req;

  req.req_header_.set_version(0);
  req.req_header_.set_call_type(0);
  req.req_header_.set_request_id(1);
  req.req_header_.set_timeout(1000);
  req.req_header_.set_caller("test_client");
  req.req_header_.set_callee("trpc.test.helloworld.Greeter");
  req.req_header_.set_func("/trpc.test.helloworld.Greeter/SayHello");

  uint32_t req_header_size = req.req_header_.ByteSizeLong();

  std::string body_str("hello world");

  uint32_t req_body_size = body_str.size();

  uint32_t total_size = TrpcFixedHeader::TRPC_PROTO_PREFIX_SPACE + req_header_size + req_body_size;

  BufferPtr buff(new Buffer(total_size));

  uint16_t magic_value = req.fixed_header_.magic_value;
  magic_value = htons(magic_value);

  memcpy(buff->GetWritePtr(), &magic_value, TrpcFixedHeader::TRPC_PROTO_MAGIC_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_MAGIC_SPACE);

  memcpy(buff->GetWritePtr(), &(req.fixed_header_.data_frame_type), TrpcFixedHeader::TRPC_PROTO_DATAFRAME_TYPE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_DATAFRAME_TYPE_SPACE);

  memcpy(buff->GetWritePtr(), &(req.fixed_header_.stream_frame_type),
         TrpcFixedHeader::TRPC_PROTO_STREAMFRAME_TYPE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_STREAMFRAME_TYPE_SPACE);

  uint32_t data_frame_size = total_size;
  data_frame_size = htonl(data_frame_size);

  memcpy(buff->GetWritePtr(), &data_frame_size, TrpcFixedHeader::TRPC_PROTO_DATAFRAME_SIZE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_DATAFRAME_SIZE_SPACE);

  uint16_t pb_header_size = req_header_size;
  pb_header_size = htons(req_header_size);

  memcpy(buff->GetWritePtr(), &pb_header_size, TrpcFixedHeader::TRPC_PROTO_HEADER_SIZE_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_HEADER_SIZE_SPACE);

  uint32_t stream_id = req.fixed_header_.stream_id;
  stream_id = htons(stream_id);

  memcpy(buff->GetWritePtr(), &stream_id, TrpcFixedHeader::TRPC_PROTO_STREAM_ID_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_STREAM_ID_SPACE);

  memcpy(buff->GetWritePtr(), &(req.fixed_header_.reversed), TrpcFixedHeader::TRPC_PROTO_REVERSED_SPACE);
  buff->AddWriteLen(TrpcFixedHeader::TRPC_PROTO_REVERSED_SPACE);

  req.req_header_.SerializeToArray(buff->GetWritePtr(), req_header_size);
  buff->AddWriteLen(req_header_size);

  memcpy(buff->GetWritePtr(), body_str.c_str(), body_str.size());

  in.clear();
  out.clear();

  in.push_back(buff);

  auto result = checker_->Check(conn, in, out);

  ASSERT_EQ(result, PacketChecker::PACKET_LESS);
  ASSERT_EQ(out.size(), 0);
  ASSERT_EQ(in.size(), 1);
}

}  // namespace trpc
}  // namespace polaris
