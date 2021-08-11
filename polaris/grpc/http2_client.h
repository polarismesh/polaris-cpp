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

#ifndef POLARIS_CPP_POLARIS_GRPC_HTTP2_CLIENT_H_
#define POLARIS_CPP_POLARIS_GRPC_HTTP2_CLIENT_H_

#include <nghttp2/nghttp2.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include <set>
#include <string>
#include <vector>

#include "buffer.h"
#include "header.h"
#include "polaris/log.h"
#include "reactor/event.h"
#include "reactor/reactor.h"
#include "status.h"
#include "utils/scoped_ptr.h"

namespace polaris {

namespace grpc {

void GRPC_LOG(const char* file, int line, LogLevel log_level, const char* format, ...);

// 封装NGHTTP2的callback，用于创建静态对象，所有连接均可使用
class NgHttp2Callbacks {
public:
  NgHttp2Callbacks();
  ~NgHttp2Callbacks();
  static const nghttp2_session_callbacks* callbacks();

private:
  nghttp2_session_callbacks* callbacks_;
};

// HTTP2 settings，目前不支持动态配置
namespace Http2Settings {
static const uint32_t DEFAULT_SETTINGS_HEADER_TABLE_SIZE      = (1 << 12);
static const uint32_t DEFAULT_SETTINGS_ENABLE_PUSH            = 0;
static const uint32_t DEFAULT_SETTINGS_MAX_CONCURRENT_STREAMS = 0;
static const uint32_t DEFAULT_SETTINGS_INITIAL_WINDOW_SIZE    = 4194304;
static const uint32_t DEFAULT_SETTINGS_MAX_FRAME_SIZE         = 4194304;
static const uint32_t DEFAULT_SETTINGS_MAX_HEADER_LIST_SIZE   = 8192;
// GRPC 自定义设置
static const int32_t SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA_ID       = 65027;
static const uint32_t DEFAULT_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA = 1;
};  // namespace Http2Settings

// 封装NGHTTP2的Options，用于创建静态对象，所有连接均可使用
class NgHttp2Options {
public:
  NgHttp2Options();
  ~NgHttp2Options();
  static const nghttp2_option* options();

private:
  nghttp2_option* options_;
};

// Http2Stream的回调接口，GrpcStream实现。
// Http2Stream是全双工的，即使本地到远端的流已经结束，回调通知会调用，除非远端到本地的流也关闭
class Http2StreamCallback {
public:
  virtual ~Http2StreamCallback() {}

  // 当所有头部接收完成后回调，end_stream标示流是否结束
  virtual void OnHeaders(HeaderMap* headers, bool end_stream) = 0;

  // 当流收完一个数据帧时回调，end_stream标示流是否结束
  virtual void OnData(Buffer& data, bool end_stream) = 0;

  // 当所有尾部接收完成后回调
  virtual void OnTrailers(HeaderMap* trailers) = 0;

  // 流被Reset或出现异常时回调
  virtual void OnReset(GrpcStatusCode status, const std::string& message) = 0;
};

class Http2Client;
class Http2Stream {
public:
  Http2Stream(Http2Client& client, Http2StreamCallback& callback);
  ~Http2Stream() {}

  // 回调函数，用于计算pending_send_data还有多少数据待发送
  ssize_t OnDataSourceRead(uint64_t length, uint32_t* data_flags);
  // 回调函数，用于从pending_send_data读取数据到nghttp2
  int OnDataSourceSend(const uint8_t* frame_hd, size_t length);

  // GrpcStream提交到Http2Stream
  void SubmitHeaders(HeaderMap* headers);          // 连接未建立则设置pending状态待发送
  void SendPendingHeader();                        // 用于连接成功后的发送缓存的HEADERS
  void SubmitData(Buffer* data, bool end_stream);  // 保存数据到pending_send_data

  void SaveRecvHeader(HeaderEntry* header_entry);

  // 收到的数据通过回调传给grpc stream
  void DecodeHeaders();
  void DecodeData(Buffer& data, bool end_stream);
  void DecodeTrailers();

  void ResetStream(GrpcStatusCode status, const std::string& message);

  void CloseGrpcStream() { grpc_stream_close_ = true; }

private:
  // 提交数据到nghttp2，provider封装onDataSourceRead方法
  void SubmitHeaders(const std::vector<nghttp2_nv>& final_headers, nghttp2_data_provider* provider);

private:
  friend class Http2Client;
  Http2Client& client_;
  Http2StreamCallback& callback_;
  bool grpc_stream_close_;  // 标示grpc流是否关闭，关闭后不能再调用callback_的方法

  // http2 client发起异步Connect后还未成功时，grpc
  // stream发送过来的头部先给缓存起来，连接建立后才提交到nghttp2库
  ScopedPtr<HeaderMap> send_headers_;  // 请求发送的headers
  bool send_headers_is_pending_;       // 异步连接还未成功时先不发送

  int32_t stream_id_;  // Header提交到nghttp2库后，会返回Stream ID

  // 缓存grpc stream发送的数据，等待nghttp2库触发回调编码发送到服务器
  ScopedPtr<Buffer> pending_send_data_;
  // 缓存网络收到的数据，等到nghttp2库通过回调解码后发给grpc stream
  ScopedPtr<Buffer> pending_recv_data_;

  ScopedPtr<HeaderMap> recv_headers_;  // 应答包headers

  bool local_end_stream_;       // 标记本地是否将标记写入了pending_send_data_
  bool local_end_stream_sent_;  // 记录是否给nghttp2发送过end stream标记
  bool remote_end_stream_;      // 接收流上是否收到了对端的end_stream标记

  // 当nghttp2发送数据回调发现pending_send_data_没有数据时，给nghttp2返回暂时没有数据发送标记，暂停触发发送数据回调。
  // 当有数据要发送写入pending_send_data_时，通过本标记觉得是否要唤醒nghttp2来消费数据
  bool data_deferred_;
};

enum ConnectionState {
  kConnectionInit = 0,     // 初始化，未发起连接请求
  kConnectionConnecting,   // 已经异步发起连接请求
  kConnectionConnected,    // 连接成功
  kConnectionDisconnected  // 连接失败或连接关闭
};

// 限制收取的 Response-Headers, Trailers, Trailers-Only最大8KB
// 参见：https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md
static const uint64_t MAX_RECEIVE_HEADERS_SIZE = 8 * 1024;

// 非阻塞连接回调
class ConnectCallback {
public:
  virtual ~ConnectCallback() {}
  // 连接建立成功时触发
  virtual void OnSuccess() = 0;
  // 连接建立失败时出发
  virtual void OnFailed() = 0;
  // 连接超时回调
  virtual void OnTimeout() = 0;
};

// HTTP2连接，一个连接上可以管理多个Stream
class Http2Client : public EventBase {
public:
  explicit Http2Client(Reactor& reactor);
  virtual ~Http2Client();

  // 创建非阻塞连接
  bool ConnectTo(const std::string& host, int port);
  // 同步等待等待连接完成
  bool WaitConnected(int timeout);
  // 连接成功后提交到Reactor
  void SubmitToReactor();

  // 发起非阻塞连接，并将fd提交到Reactor
  // 设置连接callback在建立成功、失败或超时时调用
  void ConnectTo(const std::string& host, int port, uint64_t timeout, ConnectCallback* callback);
  // 设置连接已超时，无需再触发连接回调
  void ReleaseConnectCallback();

  // 当前连接的服务器
  const std::string& CurrentServer() { return current_server_; }

  const std::string& ClientIp() { return client_ip_; }

  // EventBase
  virtual void ReadHandler();   // 读事件
  virtual void WriteHandler();  // 写事件
  virtual void CloseHandler();  // 关闭事件

  void SendSettings();  // 发送 SETTING帧 和 WINDOW UPDATE帧
  bool WantsToWrite() { return nghttp2_session_want_write(session_); }
  void SendPendingFrames();

  // 建立一个未初始化的Http2 Stream，需要发送Header初始化
  Http2Stream* NewStream(Http2StreamCallback& callback);

  // 通过Stream id获取Stream对象
  Http2Stream* GetStream(int32_t stream_id);

  // NGHTTP2回调
  int OnBeginRecvStreamHeaders(const nghttp2_frame* frame);  // 开始处理HEADER Frame回调
  int OnRecvStreamHeader(const nghttp2_frame* frame, HeaderEntry* header_entry);  // 处理HEADER回调
  int SaveStreamHeader(const nghttp2_frame* frame, HeaderEntry* header_entry);  // 保存HEADER内容
  int OnStreamData(int32_t stream_id, const uint8_t* data, size_t len);
  int OnFrameReceived(const nghttp2_frame* frame);
  int OnFrameSend(const nghttp2_frame* frame);
  int OnInvalidFrame(int32_t stream_id, int error_code);
  ssize_t OnSend(const uint8_t* data, size_t length);
  int OnStreamClose(int32_t stream_id, uint32_t error_code);

  // 主动reset所有stream
  void ResetAllStream(GrpcStatusCode status, const std::string& message);

  // 异步连接超时触发回调
  static void OnConnectTimeout(Http2Client* client);

private:
  bool CheckSocketConnect();

  void DoSend();

  static const char* FrameTypeToCstr(uint8_t type);

private:
  friend class Http2Stream;
  Reactor& reactor_;
  ConnectionState state_;
  ScopedPtr<ConnectCallback> callback_;
  TimingTaskIter connect_timeout_iter_;
  bool attached_;
  std::string current_server_;
  std::string client_ip_;
  nghttp2_session* session_;
  std::set<Http2Stream*> stream_set_;
  Buffer socket_buffer_;
};

}  // namespace grpc
}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_GRPC_HTTP2_CLIENT_H_
