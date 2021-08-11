
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

#include "utils/netclient.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <vector>

#include "config/seed_server.h"
#include "logger.h"
#include "utils/time_clock.h"

namespace polaris {

int NetClient::CreateTcpSocket(bool non_block) {
  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0) {
    POLARIS_LOG(LOG_ERROR, "create tcp socket failed, errno:%d", errno);
    return -1;
  }
  if (non_block) {
    if (SetNonBlock(socket_fd) < 0) {
      close(socket_fd);
      return -1;
    }
  }
  SetCloExec(socket_fd);
  SetNoDelay(socket_fd);
  return socket_fd;
}

int NetClient::SetNonBlock(int fd) {
  int flag = fcntl(fd, F_GETFL);
  if (flag < 0) {
    return -1;
  }
  flag |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flag) < 0) {
    return -1;
  }
  return 0;
}

int NetClient::SetBlock(int fd) {
  int flag = fcntl(fd, F_GETFL);
  if (flag < 0) {
    POLARIS_LOG(LOG_ERROR, "fcntl get in SetBlock failed, errno = %d", errno);
    return -1;
  }
  flag &= (~O_NONBLOCK);
  if (fcntl(fd, F_SETFL, flag) < 0) {
    POLARIS_LOG(LOG_ERROR, "fcntl set in SetBlock failed, errno = %d", errno);
    return -1;
  }
  return 0;
}

int NetClient::SetCloExec(int fd) {
  int flag = fcntl(fd, F_GETFD);
  if (flag < 0) {
    return -1;
  }
  flag |= FD_CLOEXEC;
  if (fcntl(fd, F_SETFD, flag) < 0) {
    return -1;
  }
  return 0;
}

int NetClient::SetNoDelay(int socket_fd) {
  int val = 1;
  if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, static_cast<void*>(&val), sizeof(val)) < 0) {
    return -1;
  }
  return 0;
}

int NetClient::CloseNoLinger(int socket_fd) {
  struct linger lin;
  lin.l_onoff  = 1;
  lin.l_linger = 0;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_LINGER, static_cast<void*>(&lin), sizeof(lin)) < 0) {
    POLARIS_LOG(LOG_ERROR, "setsockopt SO_LINGER failed, errno = %d", errno);
  }
  close(socket_fd);
  return 0;
}

int NetClient::ConnectWithTimeout(int socket_fd, const std::string& host, int port,
                                  int timeout_ms) {
  struct sockaddr_in addr;
  bzero(static_cast<void*>(&addr), sizeof(struct sockaddr_in));
  addr.sin_family      = AF_INET;
  addr.sin_port        = htons(port);
  addr.sin_addr.s_addr = inet_addr(host.c_str());

  struct pollfd poll_fd;

  int retcode = connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr));
  if (retcode >= 0) {
    return 0;
  }
  if (errno != EINPROGRESS) {
    POLARIS_LOG(LOG_ERROR, "host = %s:%d, tcp connect directly failed, errno = %d", host.c_str(),
                port, errno);
    return -1;
  }
  while (true) {
    poll_fd.fd     = socket_fd;
    poll_fd.events = POLLIN | POLLOUT;
    int ret        = poll(&poll_fd, 1, timeout_ms);
    if (ret == -1) {
      if (errno == EINTR) {
        continue;
      }
      POLARIS_LOG(LOG_ERROR, "host = %s:%d, tcp connect poll failed, errno = %d", host.c_str(),
                  port, errno);
      return -1;
    } else if (ret == 0) {  // timeout
      POLARIS_LOG(LOG_ERROR, "host = %s:%d, tcp connect timeout, timeout_ms = %d", host.c_str(),
                  port, timeout_ms);
      return -1;
    } else {
      int val       = 0;
      socklen_t len = sizeof(val);
      ret           = getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, static_cast<void*>(&val), &len);
      if (ret == -1) {
        POLARIS_LOG(LOG_ERROR, "host = %s:%d, tcp connect getsockopt failed, errno = %d",
                    host.c_str(), port, errno);
        return -1;
      }
      if (val != 0) {
        POLARIS_LOG(LOG_ERROR, "host = %s:%d, tcp connect failed, errno = %d", host.c_str(), port,
                    errno);
        return -1;
      }
      return 0;
    }
  }
}

int NetClient::TcpSendRecv(const std::string& host, int port, int timeout_ms,
                           const std::string& send_package, std::string* recv_package) {
  int64_t start_time_ms = static_cast<int64_t>(Time::GetCurrentTimeMs());
  if (timeout_ms <= 0) {
    timeout_ms = kDefaultTimeoutMs;
  }
  int64_t left_time = timeout_ms;
  int socket_fd     = CreateTcpSocket(true);
  if (socket_fd < 0) {
    return -1;
  }

  if (ConnectWithTimeout(socket_fd, host, port, left_time) < 0) {
    CloseNoLinger(socket_fd);
    return -1;
  }

  // 连接成功
  if (send_package.empty()) {  // 发送包为空，可以认为是连接探测，则直接立即关闭连接
    CloseNoLinger(socket_fd);
    return 0;
  }

  left_time = timeout_ms - (static_cast<int64_t>(Time::GetCurrentTimeMs()) - start_time_ms);
  if (left_time <= 0) {  // 已经超时，直接返回
    close(socket_fd);
    return -1;
  }

  SetBlock(socket_fd);  // 重新设置为block模式

  struct timeval timeout_val;
  timeout_val.tv_sec  = left_time / 1000;
  timeout_val.tv_usec = (left_time % 1000) * 1000;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (const void*)&timeout_val,
                 sizeof(timeout_val)) < 0) {
    POLARIS_LOG(LOG_ERROR, "setsockopt SO_SNDTIMEO failed, errno = %d", errno);
  }
  ssize_t bytes = send(socket_fd, send_package.data(), send_package.size(), 0);
  if (bytes < 0) {
    POLARIS_LOG(LOG_ERROR, "host = %s:%d, send package failed, errno = %d", host.c_str(), port,
                errno);
    close(socket_fd);
    return -1;
  }

  if (NULL == recv_package) {  // 接收缓存为空，则认为只发不收
    close(socket_fd);
    return 0;
  }

  left_time = timeout_ms - (static_cast<int64_t>(Time::GetCurrentTimeMs()) - start_time_ms);
  if (left_time <= 0) {  // 已经超时，直接返回
    close(socket_fd);
    return -1;
  }
  timeout_val.tv_sec  = left_time / 1000;
  timeout_val.tv_usec = (left_time % 1000) * 1000;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const void*)&timeout_val,
                 sizeof(timeout_val)) < 0) {
    POLARIS_LOG(LOG_ERROR, "setsockopt SO_RCVTIMEO failed, errno = %d", errno);
  }
  char buffer[1024] = {0};
  bytes             = recv(socket_fd, buffer, sizeof(buffer), 0);
  if (bytes <= 0) {
    if (bytes == 0) {
      POLARIS_LOG(LOG_ERROR, "host = %s:%d, recv failed with peer closed", host.c_str(), port);
    } else {
      POLARIS_LOG(LOG_ERROR, "host = %s:%d, recv failed with errno:%d", host.c_str(), port, errno);
    }
    close(socket_fd);
    return -1;
  }
  close(socket_fd);
  recv_package->assign(buffer, bytes);
  return 0;
}

int NetClient::UdpSendRecv(const std::string& host, int port, int timeout_ms,
                           const std::string& send_package, std::string* recv_package) {
  if (send_package.empty()) {
    POLARIS_LOG(LOG_ERROR, "send package is empty");
    return -1;
  }

  int64_t start_time_ms = static_cast<int64_t>(Time::GetCurrentTimeMs());
  int64_t left_time     = timeout_ms;
  if (timeout_ms <= 0) {
    timeout_ms = kDefaultTimeoutMs;
  }
  int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd < 0) {
    POLARIS_LOG(LOG_ERROR, "create udp socket failed, errno:%d", errno);
    return -1;
  }

  struct sockaddr_in dst_addr;
  dst_addr.sin_family      = AF_INET;
  dst_addr.sin_port        = htons(port);
  dst_addr.sin_addr.s_addr = inet_addr(host.c_str());

  struct timeval timeout_val;
  timeout_val.tv_sec  = left_time / 1000;
  timeout_val.tv_usec = (left_time % 1000) * 1000;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (const void*)&timeout_val,
                 sizeof(timeout_val)) < 0) {
    POLARIS_LOG(LOG_ERROR, "setsockopt SO_SNDTIMEO failed, errno = %d", errno);
  }
  ssize_t bytes = sendto(socket_fd, send_package.data(), send_package.size(), 0,
                         (struct sockaddr*)&dst_addr, sizeof(dst_addr));
  if (bytes < 0) {
    POLARIS_LOG(LOG_ERROR, "host = %s:%d, send package failed, errno = %d", host.c_str(), port,
                errno);
    close(socket_fd);
    return -1;
  }

  left_time = timeout_ms - (static_cast<int64_t>(Time::GetCurrentTimeMs()) - start_time_ms);
  if (left_time <= 0) {  // 已经超时，直接返回
    close(socket_fd);
    return -1;
  }
  timeout_val.tv_sec  = left_time / 1000;
  timeout_val.tv_usec = (left_time % 1000) * 1000;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const void*)&timeout_val,
                 sizeof(timeout_val)) < 0) {
    POLARIS_LOG(LOG_ERROR, "setsockopt SO_RCVTIMEO failed, errno = %d", errno);
  }
  char buffer[1024] = {0};
  bytes             = recvfrom(socket_fd, buffer, sizeof(buffer), 0, NULL, NULL);
  if (bytes < 0) {
    POLARIS_LOG(LOG_ERROR, "host = %s:%d, recv package failed, errno = %d", host.c_str(), port,
                errno);
    close(socket_fd);
    return -1;
  }
  if (recv_package) {
    recv_package->assign(buffer, bytes);
  }
  close(socket_fd);
  return 0;
}

bool NetClient::GetIpByIf(const std::string& ifname, std::string* ip) {
  if (ifname.empty() || ip == NULL) {
    return false;
  }
  int fd, intrface;
  struct ifreq buf[10];
  struct ifconf ifc = {0, {0}};
  memset(buf, 0, sizeof(buf));
  struct in_addr addr;
  char addr_buffer[32] = {0};

  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = (caddr_t)buf;
    if (!ioctl(fd, SIOCGIFCONF, &ifc)) {
      intrface = ifc.ifc_len / sizeof(struct ifreq);
      while (intrface-- > 0) {
        if (strcmp(buf[intrface].ifr_name, ifname.c_str()) == 0) {
          if (!(ioctl(fd, SIOCGIFADDR, &buf[intrface]))) {
            memset(addr_buffer, 0, sizeof(addr_buffer));
            addr = ((struct sockaddr_in*)(&buf[intrface].ifr_addr))->sin_addr;
            if (inet_ntop(AF_INET, static_cast<void*>(&addr), addr_buffer, sizeof(addr_buffer))) {
              ip->assign(addr_buffer);
              close(fd);
              return true;
            }
          }
          break;
        }
      }
    }
    close(fd);
  }
  return false;
}

bool NetClient::GetIpByConnect(std::string* ip) {
  std::vector<SeedServer> default_servers;
  SeedServerConfig::GetDefaultSeedServer(default_servers);
  POLARIS_ASSERT(!default_servers.empty());
  SeedServer& server = default_servers[time(NULL) % default_servers.size()];

  int socket_fd = CreateTcpSocket(true);
  if (socket_fd < 0) {
    POLARIS_LOG(LOG_ERROR, "get local ip by connect to server[%s:%d] with create socket errno: %d",
                server.ip_.c_str(), server.port_, errno);
    return false;
  }
  ConnectWithTimeout(socket_fd, server.ip_, server.port_, 200);
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  if (getsockname(socket_fd, (struct sockaddr*)&addr, &len) < 0) {
    POLARIS_LOG(LOG_INFO, "get local ip by connect to server[%s:%d] with errno: %d",
                server.ip_.c_str(), server.port_, errno);
    CloseNoLinger(socket_fd);
    return false;
  }
  ip->assign(inet_ntoa(addr.sin_addr));
  CloseNoLinger(socket_fd);
  return true;
}

}  // namespace polaris
