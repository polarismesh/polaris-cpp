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

#include "network/socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "logger.h"
#include "utils/netclient.h"

namespace polaris {

Socket::Socket() : fd_(-1) {}

Socket::Socket(int fd) : fd_(fd) {}

Socket::Socket(const Socket& other) : fd_(other.fd_) {}

Socket& Socket::operator=(const Socket& other) {
  fd_ = other.fd_;
  return *this;
}

// 创建Tcp socket
Socket Socket::CreateTcpSocket(bool ipv6) {
  int domain = ipv6 ? AF_INET6 : AF_INET;
  int fd = ::socket(domain, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    POLARIS_LOG(LOG_ERROR, "create socket failed, errno:%d, error msg:%s", errno, strerror(errno));
  }
  NetClient::SetCloExec(fd);
  return Socket(fd);
}

int Socket::Accept(NetworkAddress* peer_addr) {
  int fd = -1;
  struct sockaddr addr;
  socklen_t len = static_cast<socklen_t>(sizeof(addr));
  while ((fd = ::accept4(fd_, &addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC)) < 0 && errno == EINTR) {
  }

  if (peer_addr != nullptr) {
    *peer_addr = NetworkAddress(&addr);
  }

  return fd;
}

void Socket::SetReuseAddr() {
  // 如果服务器终止后,服务器可以第二次快速启动而不用等待一段时间
  int flag = 1;

  // 设置
  if (SetSockOpt(SO_REUSEADDR, static_cast<const void*>(&flag), static_cast<socklen_t>(sizeof(flag)), SOL_SOCKET) ==
      -1) {
    POLARIS_ASSERT(false);
  }
}

void Socket::Bind(const NetworkAddress& bind_addr) {
  int ret = ::bind(fd_, bind_addr.Sockaddr(), bind_addr.Socklen());
  if (ret != 0) {
    POLARIS_LOG(LOG_ERROR, "Bind address:%s, error:%s", bind_addr.ToString().c_str(), strerror(errno));
    POLARIS_ASSERT(false);
  }
}

void Socket::Close() {
  if (fd_ != -1) {
    ::close(fd_);
    fd_ = -1;
  }
}

void Socket::Listen(int backlog) {
  if (::listen(fd_, backlog) < 0) {
    POLARIS_ASSERT(false);
  }
}

int Socket::Connect(const NetworkAddress& addr) {
  if (::connect(fd_, addr.Sockaddr(), addr.Socklen()) < 0 && errno != EINPROGRESS) {
    return -1;
  }
  return 0;
}

int Socket::Recv(void* buff, size_t len, int flag) { return ::recv(fd_, buff, len, flag); }

int Socket::Send(const void* buff, size_t len, int flag) { return ::send(fd_, buff, len, flag); }

int Socket::RecvFrom(void* buff, size_t len, int flag, NetworkAddress* peer_addr) {
  struct sockaddr addr;
  socklen_t sock_len = static_cast<socklen_t>(sizeof(addr));
  int ret = ::recvfrom(fd_, buff, len, flag, &addr, &sock_len);
  if (ret > 0 && peer_addr != nullptr) {
    *peer_addr = NetworkAddress(&addr);
  }
  return ret;
}

int Socket::SendTo(const void* buff, size_t len, int flag, const NetworkAddress& peer_addr) {
  return ::sendto(fd_, buff, len, flag, peer_addr.Sockaddr(), peer_addr.Socklen());
}

int Socket::Writev(const struct iovec* iov, int iovcnt) { return ::writev(fd_, iov, iovcnt); }

int Socket::SendMsg(const struct msghdr* msg, int flag) { return ::sendmsg(fd_, msg, flag); }

void Socket::SetBlock(bool block) {
  int val = 0;

  if ((val = ::fcntl(fd_, F_GETFL, 0)) == -1) {
    POLARIS_LOG(LOG_ERROR, "setsockopt failed, fd:%d, errno:%d, error msg:%s", fd_, errno, strerror(errno));
  }

  if (!block) {
    val |= O_NONBLOCK;
  } else {
    val &= ~O_NONBLOCK;
  }

  if (::fcntl(fd_, F_SETFL, val) == -1) {
    POLARIS_LOG(LOG_ERROR, "setsockopt failed, fd:%d, errno:%d, error msg:%s", fd_, errno, strerror(errno));
  }
}

int Socket::SetSockOpt(int opt, const void* val, socklen_t opt_len, int level) {
  return ::setsockopt(fd_, level, opt, val, opt_len);
}

int Socket::GetSockOpt(int opt, void* val, socklen_t* opt_len, int level) {
  return ::getsockopt(fd_, level, opt, val, opt_len);
}

void Socket::SetNoCloseWait() {
  struct linger ling;
  ling.l_onoff = 1;   // 在close socket调用后, 但是还有数据没发送完毕的时候容许逗留
  ling.l_linger = 0;  // 容许逗留的时间为0秒

  if (SetSockOpt(SO_LINGER, static_cast<const void*>(&ling), static_cast<socklen_t>(sizeof(linger)), SOL_SOCKET) ==
      -1) {
    POLARIS_LOG(LOG_ERROR, "setsockopt failed, fd:%d, errno:%d, error msg:%s", fd_, errno, strerror(errno));
  }
}

void Socket::SetCloseWait(int delay) {
  struct linger ling;
  ling.l_onoff = 1;       // 在close socket调用后, 但是还有数据没发送完毕的时候容许逗留
  ling.l_linger = delay;  // 容许逗留的时间为delay秒

  if (SetSockOpt(SO_LINGER, static_cast<const void*>(&ling), static_cast<socklen_t>(sizeof(linger)), SOL_SOCKET) ==
      -1) {
    POLARIS_LOG(LOG_ERROR, "setsockopt failed, fd:%d, errno:%d, error msg:%s", fd_, errno, strerror(errno));
  }
}

void Socket::SetCloseWaitDefault() {
  struct linger ling;
  ling.l_onoff = 0;
  ling.l_linger = 0;

  if (SetSockOpt(SO_LINGER, static_cast<const void*>(&ling), static_cast<socklen_t>(sizeof(linger)), SOL_SOCKET) ==
      -1) {
    POLARIS_LOG(LOG_ERROR, "setsockopt failed, fd:%d, errno:%d, error msg:%s", fd_, errno, strerror(errno));
  }
}

void Socket::SetTcpNoDelay() {
  int flag = 1;

  if (SetSockOpt(TCP_NODELAY, static_cast<const void*>(&flag), static_cast<socklen_t>(sizeof(flag)), IPPROTO_TCP) ==
      -1) {
    POLARIS_LOG(LOG_ERROR, "setsockopt failed, fd:%d, errno:%d, error msg:%s", fd_, errno, strerror(errno));
  }
}

void Socket::SetKeepAlive() {
  int flag = 1;
  if (SetSockOpt(SO_KEEPALIVE, static_cast<const void*>(&flag), static_cast<socklen_t>(sizeof(flag)), SOL_SOCKET) ==
      -1) {
    POLARIS_LOG(LOG_ERROR, "setsockopt failed, fd:%d, errno:%d, error msg:%s", fd_, errno, strerror(errno));
  }
}

void Socket::SetSendBufferSize(int sz) {
  int flag = 1;
  if (SetSockOpt(SO_SNDBUF, static_cast<const void*>(&sz), static_cast<socklen_t>(sizeof(flag)), SOL_SOCKET) == -1) {
    POLARIS_LOG(LOG_ERROR, "setsockopt failed, fd:%d, errno:%d, error msg:%s", fd_, errno, strerror(errno));
  }
}

int Socket::GetSendBufferSize() {
  int sz = 0;
  socklen_t len = sizeof(sz);
  if (GetSockOpt(SO_SNDBUF, static_cast<void*>(&sz), &len, SOL_SOCKET) == -1 || len != sizeof(sz)) {
    POLARIS_LOG(LOG_ERROR, "getsockopt failed, fd:%d, errno:%d, error msg:%s", fd_, errno, strerror(errno));
  }

  return sz;
}

void Socket::SetRecvBufferSize(int sz) {
  if (SetSockOpt(SO_RCVBUF, static_cast<const void*>(&sz), static_cast<socklen_t>(sizeof(sz)), SOL_SOCKET) == -1) {
    POLARIS_LOG(LOG_ERROR, "setsockopt failed, fd:%d, errno:%d, error msg:%s", fd_, errno, strerror(errno));
  }
}

int Socket::GetRecvBufferSize() {
  int sz = 0;
  socklen_t len = sizeof(sz);
  if (GetSockOpt(SO_RCVBUF, static_cast<void*>(&sz), &len, SOL_SOCKET) == -1 || len != sizeof(sz)) {
    POLARIS_LOG(LOG_ERROR, "getsockopt failed, fd:%d, errno:%d, error msg:%s", fd_, errno, strerror(errno));
  }

  return sz;
}

}  // namespace polaris
