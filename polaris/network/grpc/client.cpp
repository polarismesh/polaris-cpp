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

#include "network/grpc/client.h"

#include <inttypes.h>
#include <stddef.h>

#include "logger.h"
#include "network/grpc/header.h"
#include "network/grpc/status.h"
#include "reactor/reactor.h"
#include "reactor/task.h"

namespace polaris {
namespace grpc {

GrpcStream::GrpcStream(Http2Client* http2_client, const std::string& call_path, uint64_t timeout,
                       GrpcStreamCallback& callback)
    : http2_client_(http2_client),
      http2_stream_(nullptr),
      call_path_(call_path),
      timeout_(timeout),
      callback_(callback),
      local_end_(false),
      remote_end_(false) {}

GrpcStream::~GrpcStream() {
  // 在Http2Client中关闭引用，避免触发回调
  http2_stream_->CloseGrpcStream();
  http2_client_ = nullptr;
}

void GrpcStream::Initialize() {
  POLARIS_ASSERT(http2_client_ != nullptr);
  http2_stream_ = http2_client_->NewStream(*this);
  POLARIS_ASSERT(http2_stream_ != nullptr);
  HeaderMap* send_headers_ = new HeaderMap();
  send_headers_->InitGrpcHeader(http2_client_->CurrentServer(), call_path_, timeout_, http2_client_->ClientIp());
  // 提交HEADERS，每个流上这个方法只能调用一次，且必须在发送数据前调用
  http2_stream_->SubmitHeaders(send_headers_);
  return;
}

bool GrpcStream::SendMessage(const google::protobuf::Message& request, bool end_stream) {
  POLARIS_ASSERT(local_end_ == false);
  if (remote_end_) {  // 如果远程已经关闭了则不能再发数据了，返回false给用户，不要再使用该对象
    return false;
  }
  this->SendMessage(GrpcCodec::SerializeToGrpcFrame(request), end_stream);
  return true;
}

void GrpcStream::SendMessage(Buffer* request, bool end_stream) {
  POLARIS_ASSERT(http2_stream_ != nullptr);  // 确保已经调用过Initialize且没有释放
  local_end_ = end_stream;
  if (remote_end_) {  // 初始化的时候失败了
    delete request;
    POLARIS_LOG(LOG_ERROR, "send request but remote closed");
    return;
  }
  http2_stream_->SubmitData(request, end_stream);
}

void GrpcStream::OnHeaders(HeaderMap* headers, bool end_stream) {
  uint64_t http2_status_code = kHttp2StatusOk;
  if (!headers->GetHttp2Status(http2_status_code)) {
    POLARIS_LOG(LOG_WARN, "get http response status from headers error");
    http2_client_->ResetAllStream(kGrpcStatusInternal, "header response without http status code");
  }
  if (http2_status_code != kHttp2StatusOk) {  // 获取到了http不等于200的去情况下
    POLARIS_LOG(LOG_TRACE, "get http response status %" PRIu64 "", http2_status_code);
    // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md requires that
    // grpc-status be used if available.
    GrpcStatusCode grpc_status_code;
    if (end_stream && headers->GetGrpcStatus(grpc_status_code)) {
      OnTrailers(headers);
      return;
    }
    // Technically this should be
    // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md
    // as given by HttpToGrpcStatus(), but the Google gRPC client treats
    // this as GrpcStatus::Canceled.
    http2_client_->ResetAllStream(kGrpcStatusCanceled, "cancel with error http response");
    delete headers;
    return;
  }
  if (end_stream) {
    OnTrailers(headers);
  } else {
    delete headers;
  }
}

void GrpcStream::OnData(Buffer& data, bool end_stream) {
  decoded_messages_.clear();
  if (!grpc_decoder_.Decode(data, decoded_messages_)) {
    http2_client_->ResetAllStream(kGrpcStatusInternal, "decode http2 data frame to grpc data error");
    return;
  }

  for (std::size_t i = 0; i < decoded_messages_.size(); ++i) {
    LengthPrefixedMessage& frame = decoded_messages_[i];
    if (frame.length_ > 0 && frame.flags_ != GRPC_FH_DEFAULT) {
      http2_client_->ResetAllStream(kGrpcStatusInternal, "decode grpc data header error");
      return;
    }
    Buffer* data = frame.data_ ? frame.data_ : new Buffer();
    frame.data_ = nullptr;
    if (!callback_.OnReceiveResponse(data)) {
      http2_client_->ResetAllStream(kGrpcStatusInternal, "decode grpc data to pb message error");
      return;
    }
  }
  if (end_stream) {
    remote_end_ = true;  // 先修改，避免在OnRemoteClose中已经释放
    callback_.OnRemoteClose("end stream with data frame");
  }
}

void GrpcStream::OnTrailers(HeaderMap* trailers) {
  GrpcStatusCode grpc_status;
  if (!trailers->GetGrpcStatus(grpc_status)) {
    grpc_status = kGrpcStatusUnknown;
  }
  std::string grpc_message = trailers->GetGrpcMessage();
  delete trailers;
  if (!remote_end_) {
    remote_end_ = true;  // 先修改，避免在OnRemoteClose中已经释放
    callback_.OnRemoteClose(grpc_message);
  }
}

void GrpcStream::OnReset(const std::string& message) {
  if (!remote_end_) {
    remote_end_ = true;  // 先修改，避免在OnRemoteClose中已经释放
    callback_.OnRemoteClose(message);
  }
}

///////////////////////////////////////////////////////////////////////////////
GrpcRequest::GrpcRequest(Http2Client* http2_client, const std::string& call_path, uint64_t timeout,
                         GrpcRequestCallback& callback)
    : GrpcStream(http2_client, call_path, timeout, *this), callback_(callback), is_response_(false) {}

void GrpcRequest::Initialize(Buffer* request) {
  GrpcStream::Initialize();
  GrpcStream::SendMessage(request, true);
}

bool GrpcRequest::OnReceiveResponse(Buffer* response) {
  if (!is_response_) {
    is_response_ = true;
    callback_.OnResponse(response);
  } else {
    delete response;
  }
  return true;
}

void GrpcRequest::OnRemoteClose(const std::string& message) {
  if (!is_response_) {
    is_response_ = true;
    callback_.OnFailure(message);
  }
}

///////////////////////////////////////////////////////////////////////////////
GrpcClient::GrpcClient(Reactor& reactor) : reactor_(reactor), http2_client_(new Http2Client(reactor)) {}

GrpcClient::~GrpcClient() {
  for (std::set<GrpcStream*>::iterator it = stream_set_.begin(); it != stream_set_.end(); ++it) {
    delete *it;
  }
  POLARIS_ASSERT(http2_client_ != nullptr);
  http2_client_->CancalConnect();
  // 由于业务线程操作reactor是线程不安全的，所以提交任务给reactor自己进行连接释放
  reactor_.SubmitTask(new DeferDeleteTask<Http2Client>(http2_client_));
  http2_client_ = nullptr;
}

void GrpcClient::Close() {
  http2_client_->CancalConnect();
  for (std::set<GrpcStream*>::iterator it = stream_set_.begin(); it != stream_set_.end(); ++it) {
    (*it)->http2_stream_->CloseGrpcStream();
  }
}

GrpcStream* GrpcClient::SendRequest(google::protobuf::Message& request, const std::string& call_path, uint64_t timeout,
                                    GrpcRequestCallback& callback) {
  Buffer* buffer = GrpcCodec::SerializeToGrpcFrame(request);
  POLARIS_ASSERT(buffer != nullptr);
  GrpcRequest* grpc_request = new GrpcRequest(http2_client_, call_path, timeout, callback);
  grpc_request->Initialize(buffer);
  stream_set_.insert(grpc_request);
  return grpc_request;
}

void GrpcClient::DeleteStream(GrpcStream* stream) {
  GrpcRequest* grpc_request = dynamic_cast<GrpcRequest*>(stream);
  if (grpc_request == nullptr) {
    return;
  }
  std::set<GrpcStream*>::iterator it = stream_set_.find(grpc_request);
  if (it == stream_set_.end()) {
    return;
  }
  grpc_request->http2_stream_->CloseAndDeleteGrpcStream();
  stream_set_.erase(grpc_request);
  reactor_.SubmitTask(new DeferDeleteTask<GrpcRequest>(grpc_request));
}

GrpcStream* GrpcClient::StartStream(const std::string& call_path, GrpcStreamCallback& callback) {
  GrpcStream* grpc_stream = new GrpcStream(http2_client_, call_path, 0, callback);
  grpc_stream->Initialize();
  stream_set_.insert(grpc_stream);
  return grpc_stream;
}

}  // namespace grpc
}  // namespace polaris
