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

#include "integration/common/http_client.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <error.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "integration/common/environment.h"

#define ERROR(FORMAT, ...)       \
  printf(FORMAT, ##__VA_ARGS__); \
  if (fd > 0) {                  \
    close(fd);                   \
  }                              \
  abort();

#define BUFFER_SIZE 2048

namespace polaris {

int HttpClient::DoRequest(const std::string& method, const std::string& path,
                          const std::string& body, int, std::string& response) {
  std::string host;
  int port;
  Environment::GetConsoleServer(host, port);
  int fd = -1;
  struct sockaddr_in addr;
  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    ERROR("create socket fd with error: %d", errno);
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(port);
  if (inet_aton(host.c_str(), &addr.sin_addr) <= 0) {
    ERROR("inet_aton host %s with error: %d", host.c_str(), errno);
  }
  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    ERROR("connect to %s:%d with error: %d", host.c_str(), port, errno);
  }
  std::stringstream packet;
  packet << method << " " << path << " HTTP/1.0\r\n";
  if (!body.empty()) {
    packet << "Content-Type: application/json;charset=utf-8\r\n"
           << "Content-Length: " << body.size() << "\r\n\r\n"
           << body;
  } else {
    packet << "\r\n";
  }
  std::string packet_data = packet.str();

  ssize_t bytes = send(fd, packet_data.c_str(), packet_data.size(), 0);
  if (bytes < 0) {
    ERROR("host = %s:%d, send package failed, errno = %d", host.c_str(), port, errno);
  }

  char buffer[2048] = {0};
  std::string read_response;
  while (true) {
    bytes = recv(fd, buffer, sizeof(buffer), 0);
    if (bytes > 0) {
      read_response.append(buffer, bytes);
    } else if (bytes == 0) {
      if (!read_response.empty()) {
        break;
      }
      ERROR("host = %s:%d, receive, but remote closed", host.c_str(), port);
    } else {
      if (errno != EAGAIN) {
        ERROR("host = %s:%d, receive failed, errmsg = %d", host.c_str(), port, errno);
      } else {  // no more data
        break;
      }
    }
  }
  int code;
  if (!ToResponse(read_response, code, response)) {
    ERROR("host = %s:%d, parese error response:%s", host.c_str(), port, read_response.c_str());
  }
  close(fd);
  return code;
}

bool HttpClient::ToResponse(const std::string& data, int& code, std::string& body) {
  std::size_t p0 = data.find("\r\n\r\n");
  if (p0 != std::string::npos) {
    p0 += 4;
    size_t p1  = data.find("Content-Length:") + 15;
    size_t p2  = data.find("\r\n", p1);
    int length = atoi(data.substr(p1, p2 - p1).data());
    if (data.size() >= p0 + length) {
      size_t p1 = data.find(" ");
      size_t p2 = data.find(" ", ++p1);
      code      = atoi(data.substr(p1, p2 - p1).data());
      body      = data.substr(p0, length);
      return true;
    }
  }
  return false;
}

}  // namespace polaris
