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

#ifndef POLARIS_CPP_TEST_MOCK_FAKE_NET_SERVER_H_
#define POLARIS_CPP_TEST_MOCK_FAKE_NET_SERVER_H_

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include "logger.h"
#include "utils/netclient.h"

namespace polaris {

enum NetServerStatus { kNetServerInit, kNetServerStart, kNetServerError, kNetServerStop };

struct NetServerParam {
  NetServerParam() : port_(0), tid_(0) {}
  NetServerParam(int port, const char *response, NetServerStatus status, pthread_t tid)
      : port_(port), response_(response), status_(status), tid_(tid) {}
  int port_;
  std::string response_;
  volatile NetServerStatus status_;
  pthread_t tid_;
};

class FakeNetServer {
public:
  static void *StartTcp(void *args);

  static void *StartUdp(void *args);
};

void *FakeNetServer::StartTcp(void *args) {
  NetServerParam &param = *static_cast<NetServerParam *>(args);
  int socket_fd         = NetClient::CreateTcpSocket(false);
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family          = AF_INET;
  addr.sin_addr.s_addr     = htonl(INADDR_ANY);
  addr.sin_port            = htons(param.port_);
  int reuse_flag           = 1;
  socklen_t reuse_flag_len = sizeof(reuse_flag);
  if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, static_cast<void *>(&reuse_flag),
                 reuse_flag_len) < 0) {
    POLARIS_LOG(LOG_ERROR, "[TCP] setsockopt SO_REUSEADDR failed, errno = %d", errno);
    param.status_ = kNetServerError;
    return NULL;
  }
  if (bind(socket_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    POLARIS_LOG(LOG_ERROR, "[TCP] bind failed, errno = %d", errno);
    param.status_ = kNetServerError;
    return NULL;
  }
  if (listen(socket_fd, 512) < 0) {
    POLARIS_LOG(LOG_ERROR, "[TCP] listen failed, errno = %d", errno);
    param.status_ = kNetServerError;
    return NULL;
  }
  POLARIS_LOG(LOG_INFO, "start local tcp server 0.0.0.0:%d", param.port_);
  param.status_ = kNetServerStart;
  while (param.status_ != kNetServerStop) {
    struct timeval tv = {0, 10000};  // 10ms
    fd_set read_fd_set;
    FD_ZERO(&read_fd_set);
    FD_SET(socket_fd, &read_fd_set);
    if (select(socket_fd + 1, &read_fd_set, NULL, NULL, &tv) <= 0) {
      continue;
    }
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int conn_fd = accept(socket_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_addr_len);
    if (conn_fd < 0) {
      POLARIS_LOG(LOG_ERROR, "[TCP] accept failed, errno = %d", errno);
      continue;
    }
    POLARIS_LOG(LOG_INFO, "[TCP] accept connection from %s:%d", inet_ntoa(client_addr.sin_addr),
                ntohs(client_addr.sin_port));
    char buffer[512]   = {0};
    ssize_t read_bytes = recv(conn_fd, buffer, sizeof(buffer), 0);
    if (read_bytes < 0) {
      POLARIS_LOG(LOG_ERROR, "[TCP] recv failed from %s:%d,  errno = %d",
                  inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), errno);
      close(conn_fd);
      continue;
    }
    POLARIS_LOG(LOG_INFO, "[TCP] recv from %s:%d, data = %s", inet_ntoa(client_addr.sin_addr),
                ntohs(client_addr.sin_port), buffer);
    usleep(10 * 1000);  // sleep 10ms
    if (!param.response_.empty()) {
      ssize_t send_bytes = send(conn_fd, param.response_.data(), param.response_.size(), 0);
      if (send_bytes < 0) {
        POLARIS_LOG(LOG_ERROR, "[TCP] send failed to %s:%d,  errno = %d",
                    inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), errno);
        close(conn_fd);
        continue;
      }
      POLARIS_LOG(LOG_INFO, "[TCP] send to %s:%d, data = %s", inet_ntoa(client_addr.sin_addr),
                  ntohs(client_addr.sin_port), param.response_.c_str());
    }
    close(conn_fd);
  }
  return NULL;
}

void *FakeNetServer::StartUdp(void *args) {
  NetServerParam &param = *static_cast<NetServerParam *>(args);
  int socket_fd         = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port        = htons(param.port_);
  if (bind(socket_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    param.status_ = kNetServerError;
    return NULL;
  }
  struct timeval tv = {0, 10000};  // 10ms
  if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const void *)&tv, sizeof(tv)) < 0) {
    param.status_ = kNetServerError;
  }
  POLARIS_LOG(LOG_INFO, "start local upd server 0.0.0.0:%d", param.port_);
  param.status_ = kNetServerStart;
  while (param.status_ != kNetServerStop) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[512]          = {0};
    ssize_t read_bytes        = recvfrom(socket_fd, buffer, sizeof(buffer), 0,
                                  reinterpret_cast<sockaddr *>(&client_addr), &client_addr_len);
    if (read_bytes < 0) {
      continue;
    }
    POLARIS_LOG(LOG_INFO, "[UDP] recv from %s:%d, data = %s", inet_ntoa(client_addr.sin_addr),
                ntohs(client_addr.sin_port), buffer);
    usleep(10 * 1000);  // sleep 10ms
    if (!param.response_.empty()) {
      ssize_t send_bytes = sendto(socket_fd, param.response_.data(), param.response_.size(), 0,
                                  reinterpret_cast<sockaddr *>(&client_addr), client_addr_len);
      if (send_bytes < 0) {
        POLARIS_LOG(LOG_ERROR, "[UDP] send failed to %s:%d,  errno = %d",
                    inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), errno);
        continue;
      }
      POLARIS_LOG(LOG_INFO, "[UDP] send to %s:%d, data = %s", inet_ntoa(client_addr.sin_addr),
                  ntohs(client_addr.sin_port), param.response_.c_str());
    }
  }
  return NULL;
}

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_MOCK_FAKE_NET_SERVER_H_