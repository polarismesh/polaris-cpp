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

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include "polaris/log.h"
#include "polaris/provider.h"

bool signal_received = false;
void SignalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
  signal_received = true;
}

struct ServerArgs {
  std::string host_;
  int port_;
  bool stop_;
};

void* UdpServer(void* args) {
  ServerArgs* server_args = static_cast<ServerArgs*>(args);
  int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd < 0) {
    std::cout << "create socket error:" << errno << " msg:" << strerror(errno) << std::endl;
    server_args->stop_ = true;
    return nullptr;
  }
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  inet_aton(server_args->host_.c_str(), &(addr.sin_addr));
  addr.sin_port = htons(server_args->port_);
  if (bind(socket_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cout << "bind " << server_args->host_ << ":" << server_args->port_ << " error:" << errno
              << " msg:" << strerror(errno) << std::endl;
    server_args->stop_ = true;
    return nullptr;
  }
  struct timeval tv = {0, 10000};  // 10ms
  if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const void*)&tv, sizeof(tv)) < 0) {
    std::cout << "setsockopt SO_RCVTIMEO error:" << errno << " msg:" << strerror(errno) << std::endl;
    server_args->stop_ = true;
    return nullptr;
  }
  while (!server_args->stop_) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[512] = {0};
    ssize_t read_bytes =
        recvfrom(socket_fd, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);
    if (read_bytes < 0) {
      continue;
    }
    std::cout << "recv from " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port)
              << ", data:" << buffer << std::endl;
    usleep(10 * 1000);  // sleep 10ms
    ssize_t send_bytes =
        sendto(socket_fd, buffer, read_bytes, 0, reinterpret_cast<sockaddr*>(&client_addr), client_addr_len);
    if (send_bytes < 0) {
      std::cout << "send failed to " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port)
                << ",  errno:" << errno << ", errmsg:" << strerror(errno) << std::endl;
    } else {
      std::cout << "send to " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port)
                << ", data:" << buffer << std::endl;
    }
  }
  return nullptr;
}

int main(int argc, char** argv) {
  if (argc < 6) {
    std::cout << "usage: " << argv[0] << " service_namespace service_name service_token host port [no_heartbeat]"
              << std::endl;
    return -1;
  }
  std::string service_namespace = argv[1];
  std::string service_name = argv[2];
  std::string service_token = argv[3];
  ServerArgs server_args;
  server_args.host_ = argv[4];
  server_args.port_ = atoi(argv[5]);
  bool no_heartbeat = argc >= 7;

  // 注册信号
  signal(SIGINT, SignalHandler);

  // 启动UDP Echo server
  server_args.stop_ = false;
  pthread_t server_tid;
  pthread_create(&server_tid, nullptr, UdpServer, &server_args);
  std::cout << "start upd server " << server_args.host_ << ":" << server_args.port_ << " success." << std::endl;

  // 启动服务成功后稍微等待2s，再去注册服务
  sleep(2);

  // 设置Logger目录和日志级别
  char temp_dir[] = "/tmp/polaris_log_XXXXXX";
  char* dir_name = mkdtemp(temp_dir);
  std::cout << "set log dir to " << dir_name << std::endl;
  polaris::SetLogDir(dir_name);
  polaris::GetLogger()->SetLogLevel(polaris::kTraceLogLevel);

  // 创建Provider API
  polaris::ProviderApi* provider = polaris::ProviderApi::CreateWithDefaultFile();
  if (provider == nullptr) {
    std::cout << "create provider api failed" << std::endl;
    server_args.stop_ = true;
    pthread_join(server_tid, nullptr);
    return -1;
  }
  sleep(2);
  // 准备服务注册请求
  polaris::InstanceRegisterRequest register_req(service_namespace, service_name, service_token, server_args.host_,
                                                server_args.port_);
  if (!no_heartbeat) {
    register_req.SetHealthCheckFlag(true);
    register_req.SetHealthCheckType(polaris::kHeartbeatHealthCheck);
    register_req.SetTtl(5);  // 5s 未上报心跳就超时
  }
  timespec ts;
  uint64_t begin, end;
  polaris::ReturnCode ret;
  std::string instance_id;
  int retry_times = 3;
  while (retry_times-- > 0) {
    clock_gettime(CLOCK_REALTIME, &ts);
    begin = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    // 调用注册接口
    register_req.SetTimeout(1000);  // API会重置超时时间，所以重试前需重新设置超时时间
    ret = provider->Register(register_req, instance_id);
    clock_gettime(CLOCK_REALTIME, &ts);
    end = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (ret == polaris::kReturnOk || ret == polaris::kReturnExistedResource) {
      break;
    }
    std::cout << "register instance with error code:" << ret << " msg:" << polaris::ReturnCodeToMsg(ret).c_str()
              << std::endl;
    if (retry_times == 0) {
      delete provider;
      server_args.stop_ = true;
      pthread_join(server_tid, nullptr);
      return -1;
    }
  }
  std::cout << "register instance return id:" << instance_id << " use time:" << (end - begin) << std::endl;
  sleep(2);

  // 循环上报心跳
  polaris::InstanceHeartbeatRequest heartbeat_req(service_token, instance_id);
  while (!signal_received) {
    if (!no_heartbeat) {
      clock_gettime(CLOCK_REALTIME, &ts);
      begin = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
      heartbeat_req.SetTimeout(300);
      ret = provider->Heartbeat(heartbeat_req);
      clock_gettime(CLOCK_REALTIME, &ts);
      end = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
      if (ret != polaris::kReturnOk) {
        std::cout << "instance heartbeat with error code:" << ret << " msg:" << polaris::ReturnCodeToMsg(ret).c_str()
                  << std::endl;
        break;
      }
      std::cout << "heartbeat instance use time:" << (end - begin) << std::endl;
    }
    sleep(2);
  }

  // 先反注册服务实例，失败多尝试几次
  retry_times = 3;
  while (retry_times-- > 0) {
    polaris::InstanceDeregisterRequest deregister_req(service_token, instance_id);
    clock_gettime(CLOCK_REALTIME, &ts);
    begin = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    deregister_req.SetTimeout(1000);
    ret = provider->Deregister(deregister_req);
    clock_gettime(CLOCK_REALTIME, &ts);
    end = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (ret == polaris::kReturnOk) {
      std::cout << "deregister instance use time:" << (end - begin) << std::endl;
      break;
    }
    std::cout << "instance deregister with error code:" << ret << " msg:" << polaris::ReturnCodeToMsg(ret).c_str()
              << std::endl;
  }
  delete provider;
  // 再停止服务
  server_args.stop_ = true;
  pthread_join(server_tid, nullptr);
  return 0;
}