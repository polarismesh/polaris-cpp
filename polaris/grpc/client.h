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

#ifndef POLARIS_CPP_POLARIS_GRPC_CLIENT_H_
#define POLARIS_CPP_POLARIS_GRPC_CLIENT_H_

#include <stdint.h>

#include <set>
#include <string>
#include <vector>

#include <google/protobuf/message.h>

#include "grpc/buffer.h"
#include "grpc/codec.h"
#include "grpc/http2.h"
#include "grpc/status.h"
#include "polaris/defs.h"
#include "utils/scoped_ptr.h"

namespace polaris {

class Reactor;

namespace grpc {

class HeaderMap;

// 请求回调接口，用于通知调用者调用结果，直接实现本接口需要调用反序列化，推荐实现模板接口RequestCallback
class GrpcRequestCallback {
public:
  virtual ~GrpcRequestCallback() {}

  // 异步请求成功时触发，只会触发一次，需要实现自己反序列化
  virtual void OnSuccess(Buffer *response) = 0;

  // 异步请求失败时触发，只会触发一次
  virtual void OnFailure(GrpcStatusCode status, const std::string &message) = 0;
};

// 封装请求回调接口，应答已经反序列化
template <typename Message>
class RequestCallback : public GrpcRequestCallback {
public:
  virtual ~RequestCallback() {}

  virtual void OnSuccess(Message *message) = 0;

private:
  virtual void OnSuccess(Buffer *response) {
    Message *message = new Message();
    if (GrpcCodec::ParseBufferToMessage(response, *message)) {
      OnSuccess(message);
    } else {
      delete message;
      OnFailure(kGrpcStatusInternal, "decode response failed");
    }
  }
};

// 流回调接口，用于通知调用者流的应答和状态
// 流是双向的，即使客户端已经关闭，另一边还是可以保持打开发送数据流
class GrpcStreamCallback {
public:
  virtual ~GrpcStreamCallback() {}

  // 当收到异步消息时回调
  // 返回值：应答可以反序列化时返回true，否则返回false触发流关闭
  virtual bool OnReceiveMessage(Buffer *response) = 0;

  // 当流被对端关闭时，或者流出现错误时，回调本接口。本接口回调后，不要再使用该流对象
  virtual void OnRemoteClose(GrpcStatusCode status, const std::string &message) = 0;
};

// 封装流回调接口，应答已经反序列化
template <typename Response>
class StreamCallback : public GrpcStreamCallback {
public:
  virtual ~StreamCallback() {}
  virtual void OnReceiveMessage(Response *message) = 0;

private:
  virtual bool OnReceiveMessage(Buffer *response) {
    Response *message = new Response();
    if (GrpcCodec::ParseBufferToMessage(response, *message)) {
      OnReceiveMessage(message);
      return true;
    } else {
      delete message;
      return false;  // 返回false，触发调用者去执行流关闭回调
    }
  }
};

// 请求回调基类
template <typename R>
class RpcCallback {
public:
  virtual ~RpcCallback() {}

  virtual void OnSuccess(R *response) = 0;  // 远程调用成功时触发回调

  virtual void OnError(ReturnCode ret_code) = 0;  // 远程调用失败时触发回调
};

// 连接回调引用包装类
template <typename R>
class ConnectCallbackRef : public ConnectCallback {
public:
  explicit ConnectCallbackRef(R &callback) : callback_(callback) {}

  virtual void OnSuccess() { callback_.OnConnectSuccess(); }
  virtual void OnFailed() { callback_.OnConnectFailed(); }
  virtual void OnTimeout() { callback_.OnConnectTimeout(); }

private:
  R &callback_;
};

// Grpc Stream，需要实现Http2StreamCallback方法
class GrpcStream : public Http2StreamCallback {
public:
  GrpcStream(Http2Client *http2_client, const std::string &call_path, uint64_t timeout,
             GrpcStreamCallback &callback);
  virtual ~GrpcStream();

  // 向Stream发送消息，如果是最后一个消息，设置end_stream为false触发本地关闭，关闭后不再调用Stream的接口
  bool SendMessage(const google::protobuf::Message &request, bool end_stream);
  // 直接触发Stream本地关闭，本地流关闭后不再主动调用流上的接口，回调可继续触发
  void SendEndStream();

protected:
  friend class Http2Stream;
  // Http2StreamCallback，处理应答
  virtual void OnHeaders(HeaderMap *headers, bool end_stream);
  virtual void OnData(Buffer &data, bool end_stream);
  virtual void OnTrailers(HeaderMap *trailers);
  virtual void OnReset(GrpcStatusCode status, const std::string &message);

protected:
  friend class GrpcClient;
  // 初始化流，构造http2 stream，并传入HEADERS
  void Initialize();

  // 序列化后的数据发送接口
  void SendMessage(Buffer *request, bool end_stream);

protected:
  Http2Client *http2_client_;
  Http2Stream *http2_stream_;

  const std::string &call_path_;  // RPC路径
  // 请求超时时间，会发送到服务器端。本地暂时不使用，本地通过在Reactor设置定时任务检查。
  // 对于流的超时本来应该是本地发送完后接收到第一个请求超过该时间，实际不这样做
  // 例如：对于服务发现而言，从服务A发现请求发出超时时间以内未收到该服务应答就算超时
  uint64_t timeout_;
  GrpcStreamCallback &callback_;

  GrpcDecoder grpc_decoder_;
  std::vector<LengthPrefixedMessage> decoded_messages_;

  bool local_end_;  // 本地流以发送结束标志，结束后不能再发请求，该标记只用于检查
  bool remote_end_;  // 远端流是否结束，结束后，再发请求也不会应答，直接返回
};

// GRPC请求，继承自Stream
class GrpcRequest : public GrpcStream, GrpcStreamCallback {
public:
  GrpcRequest(Http2Client *http2_client, const std::string &call_path, uint64_t timeout,
              GrpcRequestCallback &callback);
  virtual ~GrpcRequest() {}

private:
  friend class GrpcClient;
  void Initialize(Buffer *request);

  // 由于GrpcRequest实际上是GrpcStream，所以需要通过实现GrpcStreamCallback接口封装调用GrpcRequestCallback
  virtual bool OnReceiveMessage(Buffer *response);
  virtual void OnRemoteClose(GrpcStatusCode status, const std::string &message);

private:
  GrpcRequestCallback &callback_;
  ScopedPtr<Buffer> response_;  // 反序列化前的应答
};

// GrpcClient，非线程安全，只能在一个Reactor中运行
class GrpcClient {
public:
  explicit GrpcClient(Reactor &reactor);

  virtual ~GrpcClient();

  void CloseStream();  // 主动关闭Stream，不再触发回调

  // 封装HTTP2 client的方法
  virtual bool ConnectTo(const std::string &host, int port) {
    return http2_client_->ConnectTo(host, port);
  }
  virtual bool WaitConnected(int timeout) { return http2_client_->WaitConnected(timeout); }
  virtual void SubmitToReactor() { http2_client_->SubmitToReactor(); }
  virtual void ConnectTo(const std::string &host, int port, uint64_t timeout,
                         ConnectCallback *callback) {
    http2_client_->ConnectTo(host, port, timeout, callback);
  }
  virtual const std::string &CurrentServer() { return http2_client_->CurrentServer(); }

  // 创建call path接口的Unary RPC
  virtual void SendRequest(google::protobuf::Message &request, const std::string &call_path,
                           uint64_t timeout, GrpcRequestCallback &callback);

  // 创建call patch接口的Stream RPC
  virtual GrpcStream *StartStream(const std::string &call_path, GrpcStreamCallback &callbacks);

private:
  Reactor &reactor_;                   // 所属Reactor
  Http2Client *http2_client_;          // 当前http2连接
  std::set<GrpcStream *> stream_set_;  // 当前http2 client2上建立的grpc stream
};

}  // namespace grpc
}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_GRPC_CLIENT_H_
