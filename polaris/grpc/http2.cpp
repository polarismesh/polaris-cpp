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

#include "grpc/http2.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include "logger.h"
#include "utils/indestructible.h"
#include "utils/string_utils.h"

namespace polaris {
namespace grpc {

LogLevel GetGrpcLogLevel() {
  char* grpc_log_level_str = getenv("POLARIS_GRPC_LOG");
  if (grpc_log_level_str == NULL) {
    return kFatalLogLevel;
  }
  return static_cast<LogLevel>(grpc_log_level_str[0] - '0');
}

void GRPC_LOG(const char* file, int line, LogLevel log_level, const char* format, ...) {
  static LogLevel grpc_log_level = GetGrpcLogLevel();
  if (log_level < grpc_log_level) {
    return;
  }
  char* message = NULL;
  va_list args;
  va_start(args, format);
  if (vasprintf(&message, format, args) == -1) {
    va_end(args);
    return;
  }
  va_end(args);

  static __thread uint32_t tid = 0;
  if (tid == 0) {
    tid = syscall(SYS_gettid);
  }

  const char* display_file;
  const char* final_slash = strrchr(file, '/');
  if (final_slash == NULL) {
    display_file = file;
  } else {
    display_file = final_slash + 1;
  }
  timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  struct tm tm;
  char time_buffer[64];
  time_t timer = static_cast<time_t>(now.tv_sec);
  if (!localtime_r(&timer, &tm)) {
    snprintf(time_buffer, sizeof(time_buffer), "error:localtime");
  } else if (0 == strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &tm)) {
    snprintf(time_buffer, sizeof(time_buffer), "error:strftime");
  }

  fprintf(stderr, "[%s,%03ld] %s %s (tid:%" PRId32 " %s:%d)\n", time_buffer, now.tv_nsec / 1000000L,
          LogLevelToStr(log_level), message, tid, display_file, line);
  free(message);
}

ssize_t SessionSendCallback(nghttp2_session* /*session*/, const uint8_t* data, size_t length, int,
                            void* user_data) {
  return static_cast<Http2Client*>(user_data)->OnSend(data, length);
}

int SessionSendDataCallback(nghttp2_session* /*session*/, nghttp2_frame* frame,
                            const uint8_t* frame_hd, size_t length, nghttp2_data_source* source,
                            void*) {
  POLARIS_ASSERT(frame->data.padlen == 0);
  return static_cast<Http2Stream*>(source->ptr)->OnDataSourceSend(frame_hd, length);
}

int SessionOnBeginHeadersCallback(nghttp2_session* /*session*/, const nghttp2_frame* frame,
                                  void* user_data) {
  return static_cast<Http2Client*>(user_data)->OnBeginRecvStreamHeaders(frame);
}

int SessionOnHeaderCallback(nghttp2_session* /*session*/, const nghttp2_frame* frame,
                            const uint8_t* raw_name, size_t name_length, const uint8_t* raw_value,
                            size_t value_length, uint8_t, void* user_data) {
  HeaderEntry* header_entry = new HeaderEntry();
  header_entry->GetKey().SetCopy(reinterpret_cast<const char*>(raw_name), name_length);
  header_entry->GetValue().SetCopy(reinterpret_cast<const char*>(raw_value), value_length);
  return static_cast<Http2Client*>(user_data)->OnRecvStreamHeader(frame, header_entry);
}

int SessionOnDataChunkRecvCallback(nghttp2_session* /*session*/, uint8_t /*flags*/,
                                   int32_t stream_id, const uint8_t* data, size_t len,
                                   void* user_data) {
  return static_cast<Http2Client*>(user_data)->OnStreamData(stream_id, data, len);
}

int SessionOnFrameRecvCallback(nghttp2_session* /*session*/, const nghttp2_frame* frame,
                               void* user_data) {
  return static_cast<Http2Client*>(user_data)->OnFrameReceived(frame);
}

int SessionOnStreamCloseCallback(nghttp2_session*, int32_t stream_id, uint32_t error_code,
                                 void* user_data) {
  return static_cast<Http2Client*>(user_data)->OnStreamClose(stream_id, error_code);
}

int SessionOnFrameSendCallback(nghttp2_session* /*session*/, const nghttp2_frame* frame,
                               void* user_data) {
  return static_cast<Http2Client*>(user_data)->OnFrameSend(frame);
}

int SessionOnFrameNotSendCallback(nghttp2_session* /*session*/, const nghttp2_frame*, int, void*) {
  // We used to always return failure here but it looks now this can get called if the other
  // side sends GOAWAY and we are trying to send a SETTINGS ACK. Just ignore this for now.
  return 0;
}

int SessionOnInvalidFrameRecvCallback(nghttp2_session*, const nghttp2_frame* frame, int error_code,
                                      void* user_data) {
  return static_cast<Http2Client*>(user_data)->OnInvalidFrame(frame->hd.stream_id, error_code);
}

NgHttp2Callbacks::NgHttp2Callbacks() {
  nghttp2_session_callbacks_new(&callbacks_);
  nghttp2_session_callbacks_set_send_callback(callbacks_, SessionSendCallback);
  nghttp2_session_callbacks_set_send_data_callback(callbacks_, SessionSendDataCallback);
  nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks_,
                                                          SessionOnBeginHeadersCallback);
  nghttp2_session_callbacks_set_on_header_callback(callbacks_, SessionOnHeaderCallback);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks_,
                                                            SessionOnDataChunkRecvCallback);
  nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks_, SessionOnFrameRecvCallback);
  nghttp2_session_callbacks_set_on_stream_close_callback(callbacks_, SessionOnStreamCloseCallback);
  nghttp2_session_callbacks_set_on_frame_send_callback(callbacks_, SessionOnFrameSendCallback);
  nghttp2_session_callbacks_set_on_frame_not_send_callback(callbacks_,
                                                           SessionOnFrameNotSendCallback);
  nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(callbacks_,
                                                               SessionOnInvalidFrameRecvCallback);
}

NgHttp2Callbacks::~NgHttp2Callbacks() { nghttp2_session_callbacks_del(callbacks_); }

const nghttp2_session_callbacks* NgHttp2Callbacks::callbacks() {
  static Indestructible<NgHttp2Callbacks> http2_callbacks;  // 初始化全局静态的HTTP2 callback对象
  return http2_callbacks.Get()->callbacks_;
}

///////////////////////////////////////////////////////////////////////////////
NgHttp2Options::NgHttp2Options() {
  nghttp2_option_new(&options_);
  // Currently we do not do anything with stream priority. Setting the following option prevents
  // nghttp2 from keeping around closed streams for use during stream priority dependency graph
  // calculations. This saves a tremendous amount of memory in cases where there are a large number
  // of kept alive HTTP/2 connections.
  nghttp2_option_set_no_closed_streams(options_, 1);
  nghttp2_option_set_no_auto_window_update(options_, 1);

  // The max send header block length is configured to an arbitrarily high number so as to never
  // trigger the check within nghttp2, as we check request headers length in SaveRecvHeader.
  nghttp2_option_set_max_send_header_block_length(options_, 0x2000000);

  if (Http2Settings::DEFAULT_SETTINGS_HEADER_TABLE_SIZE != NGHTTP2_DEFAULT_HEADER_TABLE_SIZE) {
    nghttp2_option_set_max_deflate_dynamic_table_size(
        options_, Http2Settings::DEFAULT_SETTINGS_HEADER_TABLE_SIZE);
  }
}

NgHttp2Options::~NgHttp2Options() { nghttp2_option_del(options_); }

const nghttp2_option* NgHttp2Options::options() {
  static Indestructible<NgHttp2Options> http2_options;  // 初始化全局静态的HTTP2 option对象
  return http2_options.Get()->options_;
}

///////////////////////////////////////////////////////////////////////////////
Http2Stream::Http2Stream(Http2Client& client, Http2StreamCallback& callback)
    : client_(client), callback_(callback), grpc_stream_close_(false),
      send_headers_is_pending_(false), stream_id_(-1), pending_send_data_(new Buffer()),
      pending_recv_data_(new Buffer()), local_end_stream_(false), local_end_stream_sent_(false),
      remote_end_stream_(false), data_deferred_(false) {}

ssize_t Http2Stream::OnDataSourceRead(uint64_t length, uint32_t* data_flags) {
  GRPC_LOG(LOG_TRACE, "connection[%s] fd[%d] stream[%d] on data source read size=%u",
           client_.current_server_.c_str(), client_.fd_, stream_id_, length);
  if (pending_send_data_->Length() == 0 && !local_end_stream_) {
    // 客户端到服务器的流未接收，只是暂时没有数据则标记nghttp2稍后再试
    POLARIS_ASSERT(!data_deferred_);
    data_deferred_ = true;
    return NGHTTP2_ERR_DEFERRED;
  } else {
    *data_flags |= NGHTTP2_DATA_FLAG_NO_COPY;
    if (local_end_stream_ && pending_send_data_->Length() <= length) {
      *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }
    return std::min(length, pending_send_data_->Length());
  }
}

ssize_t ProviderReadCallback(nghttp2_session*, int32_t, uint8_t*, size_t length,
                             uint32_t* data_flags, nghttp2_data_source* source, void*) {
  return static_cast<Http2Stream*>(source->ptr)->OnDataSourceRead(length, data_flags);
}

void Http2Stream::SubmitHeaders(HeaderMap* headers) {
  send_headers_.Set(headers);
  // 判断Http2Client是否处于连接成功状态，如果连接成功则提交HEADERS到nghttp2库
  if (client_.state_ == kConnectionConnected) {
    std::vector<nghttp2_nv> final_headers;
    send_headers_->CopyToNghttp2Header(final_headers);
    nghttp2_data_provider provider;
    provider.source.ptr    = this;
    provider.read_callback = ProviderReadCallback;
    SubmitHeaders(final_headers, &provider);
    client_.SendPendingFrames();
    send_headers_is_pending_ = false;
  } else {
    send_headers_is_pending_ = true;
  }
}

void Http2Stream::SubmitHeaders(const std::vector<nghttp2_nv>& final_headers,
                                nghttp2_data_provider* provider) {
  POLARIS_ASSERT(stream_id_ == -1);
  stream_id_ = nghttp2_submit_request(client_.session_, NULL, &final_headers.data()[0],
                                      final_headers.size(), provider, this);
  POLARIS_ASSERT(stream_id_ > 0);
}

void Http2Stream::SendPendingHeader() {
  if (send_headers_is_pending_) {
    SubmitHeaders(send_headers_.Get());
  }
}

int Http2Stream::OnDataSourceSend(const uint8_t* frame_hd, size_t length) {
  // In this callback we are writing out a raw DATA frame without copying. nghttp2 assumes that we
  // "just know" that the frame header is 9 bytes.
  // https://nghttp2.org/documentation/types.html#c.nghttp2_send_data_callback
  static const uint64_t FRAME_HEADER_SIZE = 9;
  client_.socket_buffer_.Add(frame_hd, FRAME_HEADER_SIZE);
  client_.socket_buffer_.Move(*pending_send_data_, length);
  GRPC_LOG(LOG_TRACE, "connection[%s] fd[%d] stream[%d] write data size=%u",
           client_.current_server_.c_str(), client_.fd_, stream_id_, length + 9);
  client_.DoSend();
  return 0;
}

void Http2Stream::SubmitData(Buffer* data, bool end_stream) {
  GRPC_LOG(LOG_TRACE, "connection[%s] fd[%d] stream[%d] submit data size=%u",
           client_.current_server_.c_str(), client_.fd_, stream_id_, data->Length());
  POLARIS_ASSERT(!local_end_stream_);
  local_end_stream_ = end_stream;
  pending_send_data_->Move(*data, data->Length());
  delete data;
  if (data_deferred_) {
    int rc = nghttp2_session_resume_data(client_.session_, stream_id_);
    POLARIS_ASSERT(rc == 0);
    data_deferred_ = false;
  }
  client_.SendPendingFrames();
}

void Http2Stream::SaveRecvHeader(HeaderEntry* header_entry) {
  // recv_headers在Http2Client中OnBeginHeader回调中创建
  recv_headers_->InsertByKey(header_entry);
}

void Http2Stream::DecodeHeaders() {
  if (!grpc_stream_close_) {
    callback_.OnHeaders(recv_headers_.Release(), remote_end_stream_);
  } else {
    recv_headers_.Reset();
  }
}

void Http2Stream::DecodeData(Buffer& data, bool end_stream) {
  if (!grpc_stream_close_) {
    callback_.OnData(data, end_stream);
  }
}

void Http2Stream::DecodeTrailers() {
  if (!grpc_stream_close_) {
    callback_.OnTrailers(recv_headers_.Release());
  } else {
    recv_headers_.Reset();
  }
}

void Http2Stream::ResetStream(GrpcStatusCode status, const std::string& message) {
  if (!grpc_stream_close_) {
    callback_.OnReset(status, message);
  }
}

///////////////////////////////////////////////////////////////////////////////
Http2Client::Http2Client(Reactor& reactor)
    : EventBase(-1), reactor_(reactor), state_(kConnectionInit), callback_(NULL), attached_(false) {
  nghttp2_session_client_new2(&session_, NgHttp2Callbacks::callbacks(), this,
                              NgHttp2Options::options());
  connect_timeout_iter_ = reactor_.TimingTaskEnd();
}

Http2Client::~Http2Client() {
  nghttp2_session_del(session_);
  std::set<Http2Stream*>::iterator it;
  for (it = stream_set_.begin(); it != stream_set_.end(); ++it) {
    GRPC_LOG(LOG_TRACE, "connection[%s] fd[%d] release stream id:%d", current_server_.c_str(), fd_,
             (*it)->stream_id_);
    delete *it;
  }
  if (attached_) {
    POLARIS_ASSERT(this->fd_ >= 0);
    reactor_.RemoveEventHandler(this->fd_);
  }
  POLARIS_ASSERT(connect_timeout_iter_ == reactor_.TimingTaskEnd());
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

// TryLookup 尝试进行 host 解析
// step 1. 判断当前的 host 是否是域名
// step 2. 不是域名，则直接返回
// step 3. 是域名，进行一次域名解析，然后从返回的IPList中随机选取一个IP进行链接
static std::string TryLookup(const std::string& address) {
  GRPC_LOG(LOG_DEBUG, "try lookup address=[%s]", address.c_str());
  struct hostent* host = gethostbyname(address.c_str());
  if (!host) {
    GRPC_LOG(LOG_ERROR, "try lookup address=[%s] error, maybe address is ip", address.c_str());
    return address;
  }

  GRPC_LOG(LOG_DEBUG, "address=[%s] type: [%s]", address.c_str(),
           (host->h_addrtype == AF_INET) ? "AF_INET" : "AF_INET6");

  int total = sizeof(host->h_addr_list);
  if (total < 1) {
    return address;
  }

  std::string target_address = inet_ntoa(*(struct in_addr*)host->h_addr_list[0]);

  GRPC_LOG(LOG_TRACE, "address=[%s] select one by random [%s]", address.c_str(),
           target_address.c_str());

  return target_address;
}

// 向指定地址发起非阻塞连接
static int TryConnectTo(const std::string& host, int port, int& fd) {
  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -1;
  }
  int flag = fcntl(fd, F_GETFL);
  if (flag < 0) {
    return -1;
  }
  flag = flag | O_NONBLOCK | FD_CLOEXEC;
  if (fcntl(fd, F_SETFL, flag) < 0) {
    return -1;
  }
  int val = 1;
  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, static_cast<void*>(&val), sizeof(val)) < 0) {
    return -1;
  }
  struct sockaddr_in addr;
  bzero(static_cast<void*>(&addr), sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(port);

  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    return -1;
  }
  return connect(fd, (struct sockaddr*)&addr, sizeof(addr));
}

bool Http2Client::ConnectTo(const std::string& host, int port) {
  std::string server_ip = TryLookup(host);

  POLARIS_ASSERT(state_ == kConnectionInit);
  GRPC_LOG(LOG_INFO, "try to nonblocking connect to server[%s:%d]", server_ip.c_str(), port);
  int retcode = TryConnectTo(host, port, fd_);
  if (retcode == 0) {  // 异步连接立即成功了，一般本地连接才有可能发生。
    GRPC_LOG(LOG_TRACE, "nonblocking connect to service[%s:%d] success immediately",
             server_ip.c_str(), port);
    state_ = kConnectionConnecting;  // 即使立刻连接成功了也放在epoll写事件中去更新状态
  } else if (errno == EINPROGRESS) {  // tcp connect return -1
    state_ = kConnectionConnecting;
    GRPC_LOG(LOG_TRACE, "nonblocking connect to server[%s:%d] with connection in progress",
             server_ip.c_str(), port);
    retcode = 0;
  } else {
    state_ = kConnectionDisconnected;
    GRPC_LOG(LOG_ERROR, "nonblocking connect to %s:%d with error: %d", server_ip.c_str(), port,
             errno);
  }
  current_server_ = server_ip + ":" + StringUtils::TypeToStr<int>(port);
  return retcode == 0;
}

bool Http2Client::WaitConnected(int timeout) {
  POLARIS_ASSERT(state_ == kConnectionConnecting);
  struct pollfd poll_fd;
  poll_fd.fd     = fd_;
  poll_fd.events = POLLIN | POLLOUT;
  int ret        = poll(&poll_fd, 1, timeout);
  if (ret > 0 && CheckSocketConnect()) {
    state_ = kConnectionConnected;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(fd_, (struct sockaddr*)&addr, &len) >= 0) {
      client_ip_.assign(inet_ntoa(addr.sin_addr));
    }
    GRPC_LOG(LOG_INFO, "wait connect to server[%s] client_ip[%s] with timeout[%d] success",
             current_server_.c_str(), client_ip_.c_str(), timeout);
    return true;
  } else {
    GRPC_LOG(LOG_ERROR, "wait connect to server[%s] with timeout[%d] failed",
             current_server_.c_str(), timeout);
    return false;
  }
}

void Http2Client::SubmitToReactor() {
  POLARIS_ASSERT(state_ == kConnectionConnected);
  POLARIS_ASSERT(attached_ == false);
  reactor_.AddEventHandler(this);
  attached_ = true;
  // 连接成功，触发所有等待发起的Stream发送Headers建立流
  GRPC_LOG(LOG_INFO,
           "connection[%s] fd[%d] submit connect to reactor, send settings and %d stream header",
           current_server_.c_str(), fd_, stream_set_.size());
  SendSettings();
  for (std::set<Http2Stream*>::iterator it = stream_set_.begin(); it != stream_set_.end(); ++it) {
    (*it)->SendPendingHeader();
  }
}

void Http2Client::ConnectTo(const std::string& host, int port, uint64_t timeout,
                            ConnectCallback* callback) {
  if (ConnectTo(host, port)) {
    callback_.Set(callback);
    attached_ = true;
    reactor_.AddEventHandler(this);
    connect_timeout_iter_ =
        reactor_.AddTimingTask(new TimingFuncTask<Http2Client>(OnConnectTimeout, this, timeout));
    GRPC_LOG(LOG_INFO, "submit connect to reactor with callback server[%s] fd[%d]",
             current_server_.c_str(), fd_);
  } else {
    if (callback != NULL) {
      callback->OnFailed();
      delete callback;
    }
  }
}

void Http2Client::OnConnectTimeout(Http2Client* client) {
  client->connect_timeout_iter_ = client->reactor_.TimingTaskEnd();
  if (client->callback_.NotNull()) {
    client->callback_->OnTimeout();
    client->callback_.Reset();
  }
}

void Http2Client::ReleaseConnectCallback() {
  callback_.Reset();
  if (connect_timeout_iter_ != reactor_.TimingTaskEnd()) {
    reactor_.CancelTimingTask(connect_timeout_iter_);
    connect_timeout_iter_ = reactor_.TimingTaskEnd();
  }
}

bool Http2Client::CheckSocketConnect() {
  int val       = 0;
  socklen_t len = sizeof(val);
  int ret       = getsockopt(fd_, SOL_SOCKET, SO_ERROR, static_cast<void*>(&val), &len);
  if (ret == -1) {
    GRPC_LOG(LOG_ERROR, "check connect to server[%s] fd[%d] with getsockopt failed with errno:%d",
             current_server_.c_str(), fd_, errno);
    state_ = kConnectionDisconnected;  // 设置标记不要发数据
    this->ResetAllStream(kGrpcStatusAborted, "network connected failed");
    return false;
  }
  if (val != 0) {
    GRPC_LOG(LOG_ERROR, "check connect to server[%s] fd[%d] with errno:%d", current_server_.c_str(),
             fd_, errno);
    state_ = kConnectionDisconnected;  // 设置标记不要发数据
    this->ResetAllStream(kGrpcStatusAborted, "network connected failed");
    return false;
  }
  return true;
}

void Http2Client::ReadHandler() {
  if (state_ != kConnectionConnected && !CheckSocketConnect()) {
    if (callback_.NotNull()) {  // 异步连接失败时会触发读事件
      callback_->OnFailed();
      ReleaseConnectCallback();
    }
    return;  // 不是连接成功状态，且检查连接不正常直接返回
  }
  if (state_ == kConnectionDisconnected) {
    GRPC_LOG(LOG_DEBUG, "connection[%s] fd[%d] already disconnected but fired read event",
             current_server_.c_str(), fd_);
    return;
  }

  // 从socket中读取数据
  Buffer data;
  uint64_t bytes_read = 0;
  int read_size;
  while ((read_size = data.Read(fd_, 4000)) > 0) {  // 循环读取，读完为止
    bytes_read += read_size;
  }
  if (read_size < 0 && errno != EAGAIN) {
    GRPC_LOG(LOG_ERROR, "connection[%s] fd[%d] read event fired but read with error %d",
             current_server_.c_str(), fd_, errno);
    this->ResetAllStream(kGrpcStatusAborted, "read from socket fd failed");
    return;
  }
  if (bytes_read == 0) {
    GRPC_LOG(LOG_INFO, "connection[%s] fd[%d] read event fired and read zero bytes",
             current_server_.c_str(), fd_);
    return;
  }
  GRPC_LOG(LOG_TRACE, "connection[%s] fd[%d] read event fired and read %d bytes",
           current_server_.c_str(), fd_, bytes_read);

  // 将数据传入nghttp2 lib进行解码
  uint64_t num_slices = data.GetRawSlices(NULL, 0);
  RawSlice* slices    = new RawSlice[num_slices];
  data.GetRawSlices(slices, num_slices);
  for (uint64_t i = 0; i < num_slices; ++i) {
    RawSlice& slice = slices[i];
    ssize_t rc =
        nghttp2_session_mem_recv(session_, static_cast<const uint8_t*>(slice.mem_), slice.len_);
    if (rc == NGHTTP2_ERR_FLOODED) {
      GRPC_LOG(LOG_ERROR,
               "connection[%s] flooding was detected in this http2 session, and it must be closed",
               current_server_.c_str());
      delete[] slices;
      this->ResetAllStream(kGrpcStatusInternal, "flooding was detected in http2 session");
      return;
    }
    if (rc != static_cast<ssize_t>(slice.len_)) {
      GRPC_LOG(LOG_ERROR, "connection[%s] nghttp2 decode data exception with error: %s",
               current_server_.c_str(), nghttp2_strerror(rc));
      delete[] slices;
      this->ResetAllStream(kGrpcStatusInternal, "nghttp2 decode data error");
      return;
    }
  }
  delete[] slices;
  GRPC_LOG(LOG_TRACE, "connection[%s] http2 decode incoming %" PRIu64 " bytes",
           current_server_.c_str(), data.Length());
  data.Drain(data.Length());

  // 解码接收到的帧数据会触发生成必要的帧进行发送，所以这里触发发送
  SendPendingFrames();
}

void Http2Client::WriteHandler() {
  if (state_ != kConnectionConnected && !CheckSocketConnect()) {
    if (callback_.NotNull()) {
      callback_->OnFailed();
      ReleaseConnectCallback();
    }
    return;  // 不是连接成功状态，且检查连接不正常直接返回
  }
  if (state_ == kConnectionDisconnected) {
    GRPC_LOG(LOG_DEBUG, "connection[%s] fd[%d] already disconnected but fired write event",
             current_server_.c_str(), fd_);
    return;
  }
  GRPC_LOG(LOG_TRACE, "connection[%s] fd[%d] write event fired", current_server_.c_str(), fd_);
  if (state_ == kConnectionConnecting) {
    state_ = kConnectionConnected;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(fd_, (struct sockaddr*)&addr, &len) >= 0) {
      client_ip_.assign(inet_ntoa(addr.sin_addr));
    }
    // 连接成功后，立刻发送SETTING帧和WINDOW UPDATE帧
    SendSettings();
    GRPC_LOG(LOG_INFO, "connection[%s] fd[%d] client_ip[%s] state change to connected",
             current_server_.c_str(), fd_, client_ip_.c_str());
    if (callback_.NotNull()) {
      callback_->OnSuccess();
      callback_.Reset();
    }
    // 连接成功，触发所有等待发起的Stream发送Headers建立流
    GRPC_LOG(LOG_INFO, "connection[%s] fd[%d] submit header for %d stream", current_server_.c_str(),
             fd_, stream_set_.size());
    for (std::set<Http2Stream*>::iterator it = stream_set_.begin(); it != stream_set_.end(); ++it) {
      (*it)->SendPendingHeader();
    }
  }
  this->WantsToWrite();
  DoSend();
}

void Http2Client::CloseHandler() {
  this->ResetAllStream(kGrpcStatusOk, "remote close socket connection");
}

void Http2Client::DoSend() {
  if (socket_buffer_.Length() <= 0) {
    return;
  }
  int write_size = socket_buffer_.Write(fd_);
  if (write_size > 0) {
    GRPC_LOG(LOG_TRACE, "connection[%s] fd[%d] send size: %d", current_server_.c_str(), fd_,
             write_size);
  } else {
    GRPC_LOG(LOG_ERROR, "connection[%s] fd[%d] write data with error: %d", current_server_.c_str(),
             fd_, errno);
  }
}

// 发送Setting Frame，并且发送WINDOW UPDATE Frame更新Window到4M
void Http2Client::SendSettings() {
  std::vector<nghttp2_settings_entry> iv;
  // iv.push_back({NGHTTP2_SETTINGS_HEADER_TABLE_SIZE,
  // Http2Settings::DEFAULT_SETTINGS_HEADER_TABLE_SIZE});
  nghttp2_settings_entry entry;
  entry.settings_id = NGHTTP2_SETTINGS_ENABLE_PUSH;
  entry.value       = Http2Settings::DEFAULT_SETTINGS_ENABLE_PUSH;
  iv.push_back(entry);
  entry.settings_id = NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS;
  entry.value       = Http2Settings::DEFAULT_SETTINGS_MAX_CONCURRENT_STREAMS;
  iv.push_back(entry);
  entry.settings_id = NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE;
  entry.value       = Http2Settings::DEFAULT_SETTINGS_INITIAL_WINDOW_SIZE;
  iv.push_back(entry);
  entry.settings_id = NGHTTP2_SETTINGS_MAX_FRAME_SIZE;
  entry.value       = Http2Settings::DEFAULT_SETTINGS_MAX_FRAME_SIZE;
  iv.push_back(entry);
  entry.settings_id = NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE;
  entry.value       = Http2Settings::DEFAULT_SETTINGS_MAX_HEADER_LIST_SIZE;
  iv.push_back(entry);
  // GRPC自定义头
  entry.settings_id = Http2Settings::SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA_ID;
  entry.value       = Http2Settings::DEFAULT_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA;
  iv.push_back(entry);
  int rc = nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, &iv[0], iv.size());
  POLARIS_ASSERT(rc == 0);

  // Increase connection window size up to our default size.
  if (Http2Settings::DEFAULT_SETTINGS_INITIAL_WINDOW_SIZE != NGHTTP2_INITIAL_WINDOW_SIZE) {
    rc = nghttp2_submit_window_update(
        session_, NGHTTP2_FLAG_NONE, 0,
        Http2Settings::DEFAULT_SETTINGS_INITIAL_WINDOW_SIZE - NGHTTP2_INITIAL_WINDOW_SIZE);
    POLARIS_ASSERT(rc == 0);
  }
  GRPC_LOG(LOG_INFO, "connection[%s] submit settings and window update success",
           current_server_.c_str());
}

void Http2Client::SendPendingFrames() {
  if (state_ != kConnectionConnected) {  // 未连接成功不触发数据的发送
    return;
  }

  int rc = nghttp2_session_send(session_);
  if (rc != 0) {
    POLARIS_ASSERT(rc == NGHTTP2_ERR_CALLBACK_FAILURE);
    GRPC_LOG(LOG_ERROR, "connetion[%s] nghttp2 session send with error %s", current_server_.c_str(),
             nghttp2_strerror(rc));
  }
}

Http2Stream* Http2Client::GetStream(int32_t stream_id) {
  return static_cast<Http2Stream*>(nghttp2_session_get_stream_user_data(session_, stream_id));
}

int Http2Client::OnBeginRecvStreamHeaders(const nghttp2_frame* frame) {
  // The client code explicitly does not currently support push promise.
  POLARIS_ASSERT(frame->hd.type == NGHTTP2_HEADERS);
  POLARIS_ASSERT(frame->headers.cat == NGHTTP2_HCAT_RESPONSE ||
                 frame->headers.cat == NGHTTP2_HCAT_HEADERS);
  Http2Stream* stream = GetStream(frame->hd.stream_id);
  POLARIS_ASSERT(!stream->recv_headers_.NotNull());
  stream->recv_headers_.Set(new HeaderMap());
  if (frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
    GRPC_LOG(LOG_TRACE, "connection[%s] fd[%d] stream id %d receive first response header",
             current_server_.c_str(), fd_, frame->hd.stream_id);
  } else {
    POLARIS_ASSERT(frame->headers.cat == NGHTTP2_HCAT_HEADERS);
    GRPC_LOG(LOG_TRACE, "connection[%s] fd[%d] stream id %d receive final response header",
             current_server_.c_str(), fd_, frame->hd.stream_id);
  }
  return 0;
}

int Http2Client::OnRecvStreamHeader(const nghttp2_frame* frame, HeaderEntry* header_entry) {
  // The client code explicitly does not currently support push promise.
  POLARIS_ASSERT(frame->hd.type == NGHTTP2_HEADERS);
  POLARIS_ASSERT(frame->headers.cat == NGHTTP2_HCAT_RESPONSE ||
                 frame->headers.cat == NGHTTP2_HCAT_HEADERS);
  return SaveStreamHeader(frame, header_entry);
}

int Http2Client::SaveStreamHeader(const nghttp2_frame* frame, HeaderEntry* header_entry) {
  Http2Stream* stream = GetStream(frame->hd.stream_id);
  if (stream == NULL) {
    // We have seen 1 or 2 crashes where we get a headers callback but there is no associated
    // stream data. I honestly am not sure how this can happen. However, from reading the nghttp2
    // code it looks possible that inflate_header_block() can safely inflate headers for an already
    // closed stream, but will still call the headers callback. Since that seems possible, we should
    // ignore this case here.
    GRPC_LOG(LOG_ERROR, "connection[%s] fd[%d] recvice header but stream id %d not found",
             current_server_.c_str(), fd_, frame->hd.stream_id);
    return 0;
  }

  stream->SaveRecvHeader(header_entry);
  if (stream->recv_headers_->ByteSize() > MAX_RECEIVE_HEADERS_SIZE) {
    GRPC_LOG(LOG_ERROR, "connection[%s] fd[%d] stream id %d receive header size more than 8KB",
             current_server_.c_str(), fd_, frame->hd.stream_id);
    // This will cause the library to reset/close the stream.
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  } else {
    return 0;
  }
}

int Http2Client::OnStreamData(int32_t stream_id, const uint8_t* data, size_t len) {
  Http2Stream* stream = GetStream(stream_id);
  if (stream == NULL) {
    GRPC_LOG(LOG_WARN, "connection[%s] fd[%d] recv stream data but stream id %d not found",
             current_server_.c_str(), fd_, stream_id);
    return 0;
  }
  // 将数据加入buffer中
  stream->pending_recv_data_->Add(data, len);
  // Update the window to the peer
  nghttp2_session_consume(session_, stream_id, len);
  return len;
}

int Http2Client::OnFrameReceived(const nghttp2_frame* frame) {
  GRPC_LOG(LOG_TRACE, "connection[%s] fd[%d] recv frame type %s", current_server_.c_str(), fd_,
           FrameTypeToCstr(frame->hd.type));
  POLARIS_ASSERT(frame->hd.type != NGHTTP2_CONTINUATION);  // CONTINUATION帧在NGHTTP2库里面被处理了
  // Only raise GOAWAY once, since we don't currently expose stream information. Shutdown
  // notifications are the same as a normal GOAWAY.
  if (frame->hd.type == NGHTTP2_GOAWAY) {
    POLARIS_ASSERT(frame->hd.stream_id == 0);
    this->ResetAllStream(kGrpcStatusAborted, "server send goaway");
    return 0;
  }

  Http2Stream* stream = GetStream(frame->hd.stream_id);
  if (stream == NULL) {
    GRPC_LOG(LOG_TRACE, "recv frame type %s but stream id %d not found",
             FrameTypeToCstr(frame->hd.type), frame->hd.stream_id);
    return 0;
  } else {
    GRPC_LOG(LOG_TRACE, "recv frame type %s for stream with id %d", FrameTypeToCstr(frame->hd.type),
             frame->hd.stream_id);
  }
  switch (frame->hd.type) {
    case NGHTTP2_HEADERS: {
      stream->remote_end_stream_ = frame->hd.flags & NGHTTP2_FLAG_END_STREAM;
      switch (frame->headers.cat) {
        case NGHTTP2_HCAT_RESPONSE: {
          // stream->recv_headers_ 确定 http status != 100
          stream->DecodeHeaders();
          break;
        }
        case NGHTTP2_HCAT_REQUEST: {
          stream->DecodeHeaders();
          break;
        }
        case NGHTTP2_HCAT_HEADERS: {
          if (stream->remote_end_stream_) {
            stream->DecodeTrailers();
          } else {
            POLARIS_ASSERT(!nghttp2_session_check_server_session(session_));
            // Even if we have :status 100 in the client case in a response, when
            // we received a 1xx to start out with, nghttp2 message checking
            // guarantees proper flow here.
            stream->DecodeHeaders();
          }
          break;
        }
        default:  // We do not currently support push.
          POLARIS_ASSERT(false);
      }
      POLARIS_ASSERT(!stream->recv_headers_.NotNull());  // 数据被传回给grpc stream
      break;
    }
    case NGHTTP2_DATA: {
      stream->remote_end_stream_ = frame->hd.flags & NGHTTP2_FLAG_END_STREAM;
      stream->DecodeData(*stream->pending_recv_data_, stream->remote_end_stream_);
      stream->pending_recv_data_->Drain(stream->pending_recv_data_->Length());
      break;
    }
    case NGHTTP2_RST_STREAM: {
      GRPC_LOG(LOG_TRACE, "remote reset with error code:%" PRIu32 "", frame->rst_stream.error_code);
      break;
    }
  }

  return 0;
}

int Http2Client::OnFrameSend(const nghttp2_frame* frame) {
  // The nghttp2 library does not cleanly give us a way to determine whether we received invalid
  // data from our peer. Sometimes it raises the invalid frame callback, and sometimes it does not.
  // In all cases however it will attempt to send a GOAWAY frame with an error status. If we see
  // an outgoing frame of this type, we will return an error code so that we can abort execution.
  GRPC_LOG(LOG_TRACE, "connection[%s] fd[%d] sent frame type=%s", current_server_.c_str(), fd_,
           FrameTypeToCstr(frame->hd.type));
  switch (frame->hd.type) {
    case NGHTTP2_GOAWAY: {
      GRPC_LOG(LOG_DEBUG, "sent goaway code=%" PRIu32 "", frame->goaway.error_code);
      if (frame->goaway.error_code != NGHTTP2_NO_ERROR) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
      }
      break;
    }
    case NGHTTP2_RST_STREAM: {
      GRPC_LOG(LOG_DEBUG, "sent reset code=%" PRIu32 "", frame->rst_stream.error_code);
      break;
    }
    case NGHTTP2_HEADERS:
    case NGHTTP2_DATA: {
      Http2Stream* stream            = GetStream(frame->hd.stream_id);
      stream->local_end_stream_sent_ = frame->hd.flags & NGHTTP2_FLAG_END_STREAM;
      break;
    }
  }
  return 0;
}

int Http2Client::OnInvalidFrame(int32_t stream_id, int error_code) {
  GRPC_LOG(LOG_DEBUG, "connection[%s] fd[%d] invalid frame: %s on stream %d",
           current_server_.c_str(), fd_, nghttp2_strerror(error_code), stream_id);
  // Cause ReadHandler to return with an error code.
  return NGHTTP2_ERR_CALLBACK_FAILURE;
}

ssize_t Http2Client::OnSend(const uint8_t* data, size_t length) {
  GRPC_LOG(LOG_TRACE, "connection[%s] fd[%d] on send data size: %d", current_server_.c_str(), fd_,
           length);
  socket_buffer_.Add(data, length);
  DoSend();
  return static_cast<ssize_t>(length);
}

int Http2Client::OnStreamClose(int32_t stream_id, uint32_t error_code) {
  Http2Stream* stream = GetStream(stream_id);
  if (stream != NULL) {
    GRPC_LOG(LOG_DEBUG, "connection[%s] fd[%d] stream id[%d] closed with error code: %" PRIu32 "",
             current_server_.c_str(), fd_, stream_id, error_code);
    if (!stream->remote_end_stream_ || !stream->local_end_stream_) {
      stream->ResetStream(kGrpcStatusInternal, "stream closed before stream end");
    }
    nghttp2_session_set_stream_user_data(session_, stream->stream_id_, NULL);
  }
  return 0;
}

Http2Stream* Http2Client::NewStream(Http2StreamCallback& callback) {
  Http2Stream* stream = new Http2Stream(*this, callback);
  stream_set_.insert(stream);
  return stream;
}

void Http2Client::ResetAllStream(GrpcStatusCode status, const std::string& message) {
  GRPC_LOG(LOG_DEBUG, "connection[%s] fd[%d] reset all stream with error: %s",
           current_server_.c_str(), fd_, message.c_str());
  for (std::set<Http2Stream*>::iterator it = stream_set_.begin(); it != stream_set_.end(); ++it) {
    (*it)->ResetStream(status, message);
  }
}

const char* Http2Client::FrameTypeToCstr(uint8_t type) {
  switch (type) {
    case NGHTTP2_DATA:
      return "DATA";
    case NGHTTP2_HEADERS:
      return "HEADERS";
    case NGHTTP2_PRIORITY:
      return "PRIORITY";
    case NGHTTP2_RST_STREAM:
      return "RST_STREAM";
    case NGHTTP2_SETTINGS:
      return "SETTINGS";
    case NGHTTP2_PUSH_PROMISE:
      return "PUSH_PROMISE";
    case NGHTTP2_PING:
      return "PING";
    case NGHTTP2_GOAWAY:
      return "GOAWAY";
    case NGHTTP2_WINDOW_UPDATE:
      return "WINDOW_UPDATE";
    case NGHTTP2_CONTINUATION:  // 该类型帧被nghttp2库处理不会透传出来，在HEADERS/PUSH_PROMISE帧后面
      return "CONTINUATION";
    case NGHTTP2_ALTSVC:
      return "ALTSVC";
    case NGHTTP2_ORIGIN:
      return "ORIGIN";
    default:
      return "UNKNOW";
  }
}

}  // namespace grpc
}  // namespace polaris
