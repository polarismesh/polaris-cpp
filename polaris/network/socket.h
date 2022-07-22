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

#ifndef POLARIS_CPP_POLARIS_NETWORK_SOCKET_H_
#define POLARIS_CPP_POLARIS_NETWORK_SOCKET_H_

#include <netinet/in.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "network/address.h"

namespace polaris {

class Socket {
 public:
  Socket();

  explicit Socket(int fd);

  Socket(const Socket&);

  Socket& operator=(const Socket& other);

  ~Socket() = default;

  // 创建Tcp socket
  static Socket CreateTcpSocket(bool ipv6 = false);

  // 获取socket的文件描述符
  int GetFd() const { return fd_; }

  // 判断socket是否有效
  bool IsValid() const { return fd_ != -1; }

  // 关闭socket
  void Close();

  // 设置socket选项
  int SetSockOpt(int opt, const void* val, socklen_t opt_len, int level = SOL_SOCKET);

  // 获取socket选项
  int GetSockOpt(int opt, void* val, socklen_t* opt_len, int level = SOL_SOCKET);

  // 接收网络连接
  int Accept(NetworkAddress* peer_addr);

  // 绑定网络地址
  void Bind(const NetworkAddress& bind_addr);

  // 监听连接请求
  void Listen(int backlog = SOMAXCONN);

  // 向网络地址发起连接
  int Connect(const NetworkAddress& addr);

  // 接收数据，调用recv()网络函数
  int Recv(void* buff, size_t len, int flag = 0);

  // 发送数据，调用send()网络函数
  int Send(const void* buff, size_t len, int flag = 0);

  // 接收数据，调用recvfrom()网络函数
  int RecvFrom(void* buff, size_t len, int flag, NetworkAddress* peer_addr);

  // 发送数据，调用sendto()网络函数
  int SendTo(const void* buff, size_t len, int flag, const NetworkAddress& peer_addr);

  // 发送数据，调用writev()网络函数
  int Writev(const struct iovec* iov, int iovcnt);

  // 发送数据，调用sendmsg()网络函数
  int SendMsg(const struct msghdr* msg, int flag = 0);

  // 设置SO_REUSEADDR，服务器终止后，服务器可以第二次快速启动而不用等待一段时间
  void SetReuseAddr();

  // 设置socket阻塞/非阻塞
  void SetBlock(bool block = false);

  // 设置socket close时丢弃发送缓冲区中的全部数据
  void SetNoCloseWait();

  // 设置socket close时等待缓冲区继续发送的时间
  void SetCloseWait(int delay = 30);

  // 设置socket close为内核默认行为
  void SetCloseWaitDefault();

  // 设置允许小包发送（禁用Nagle算法）
  void SetTcpNoDelay();

  // 开启保活探测
  void SetKeepAlive();

  // 获取接收缓冲区大小
  int GetRecvBufferSize();

  // 设置接收缓冲区大小
  void SetRecvBufferSize(int sz);

  // 获取发送缓冲区大小
  int GetSendBufferSize();

  // 设置发送缓冲区大小
  void SetSendBufferSize(int sz);

 private:
  int fd_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_NETWORK_SOCKET_H_