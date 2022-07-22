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

#ifndef POLARIS_CPP_POLARIS_UTILS_NETCLIENT_H_
#define POLARIS_CPP_POLARIS_UTILS_NETCLIENT_H_

#include <string>

namespace polaris {

const uint64_t kDefaultTimeoutMs = 500;  // 默认超时时间500ms

class NetClient {
 public:
  static int CreateTcpSocket(bool non_block);

  static int SetNonBlock(int fd);

  static int SetBlock(int fd);

  static int SetCloExec(int fd);

  static int SetNoDelay(int socket_fd);

  static int CloseNoLinger(int fd);

  static int ConnectWithTimeout(int fd, const std::string& host, int port, int timeout_ms);

  static int TcpSendRecv(const std::string& host, int port, uint64_t timeout_ms, const std::string& send_package,
                         std::string* recv_package);

  static int UdpSendRecv(const std::string& host, int port, uint64_t timeout_ms, const std::string& send_package,
                         std::string* recv_package);

  static bool GetIpByIf(const std::string& ifname, std::string* ip);

  static bool GetIpByConnect(std::string* ip);
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_UTILS_NETCLIENT_H_
