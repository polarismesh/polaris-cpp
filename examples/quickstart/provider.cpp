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
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "polaris/provider.h"

class ProviderServer {
 public:
  ProviderServer(const std::string& service_namespace, const std::string& service_name,
                 const std::string& service_token, const std::string& host, int port);

  ~ProviderServer();

  // 启动服务
  int Start();

  // 注册服务实例
  int Register();

  // 反注册服务实例
  void Deregister();

  // 停止服务
  void Stop();

 private:
  std::string service_namespace_;
  std::string service_name_;
  std::string service_token_;
  std::string host_;
  int port_;
  std::string instance_id_;

  std::atomic<bool> stop_;
  std::unique_ptr<std::thread> accept_thread_;

  std::unique_ptr<polaris::ProviderApi> provider_;
  std::unique_ptr<std::thread> heartbeat_thread_;
};

bool signal_received = false;
void SignalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
  signal_received = true;
}

constexpr auto kHeartbeatTtl = 5;

int main(int argc, char** argv) {
  if (argc < 6) {
    std::cout << "usage: " << argv[0] << " service_namespace service_name service_token host port" << std::endl;
    return -1;
  }
  // 注册信号
  signal(SIGINT, SignalHandler);

  ProviderServer server(argv[1], argv[2], argv[3], argv[4], atoi(argv[5]));

  // 先启动服务
  if (server.Start() != 0) {
    return -2;
  }

  // 启动服务成功以后 再注册服务实例， 并开启心跳上报
  if (server.Register() != 0) {
    return -3;
  }

  // 循环等待退出信号
  while (!signal_received) {
    sleep(1);
  }

  // 先反注册实例
  server.Deregister();

  // 反注册完成以后再停止服务
  server.Stop();

  return 0;
}

ProviderServer::ProviderServer(const std::string& service_namespace, const std::string& service_name,
                               const std::string& service_token, const std::string& host, int port)
    : service_namespace_(service_namespace),
      service_name_(service_name),
      service_token_(service_token),
      host_(host),
      port_(port),
      stop_(false) {}

ProviderServer::~ProviderServer() { Stop(); }

int ProviderServer::Start() {
  // create a socket
  auto sock_listener = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_listener < 0) {
    std::cerr << "create socket with error: " << errno << std::endl;
    return -1;
  }

  // address info to bind socket
  sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr);
  server_addr.sin_port = htons(port_);

  // bind socket
  if (bind(sock_listener, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
    std::cerr << "bind to " << host_ << ":" << port_ << " failed with error: " << errno << std::endl;
    close(sock_listener);
    return -2;
  }

  // start listening
  if (listen(sock_listener, SOMAXCONN) < 0) {
    std::cerr << "listen to " << host_ << ":" << port_ << " failed with error: " << errno << std::endl;
    close(sock_listener);
    return -3;
  }
  std::cout << "listen to " << host_ << ":" << port_ << " success" << std::endl;

  // create accept thread
  accept_thread_ = std::unique_ptr<std::thread>(new std::thread([=] {
    while (!stop_) {
      fd_set set;
      FD_ZERO(&set);
      FD_SET(sock_listener, &set);
      struct timeval timeout;
      timeout.tv_sec = 2;
      timeout.tv_usec = 0;
      int ret = select(sock_listener + 1, &set, NULL, NULL, &timeout);
      if (ret <= 0) {
        continue;
      }
      sockaddr_in client_addr;
      socklen_t client_addr_size = sizeof(client_addr);
      int sock_client;
      if ((sock_client = accept(sock_listener, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_size)) < 0) {
        std::cerr << "accept connection failed with error:" << errno << std::endl;
        continue;
      }

      // 处理客户端连接
      std::async(std::launch::async, [=] {
        char buffer[1024];
        auto bytes = recv(sock_client, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
          std::cerr << "received message failed: " << errno << std::endl;
          close(sock_client);
          return;
        }

        std::string response = "response form " + host_ + ":" + std::to_string(port_) + " echo " + buffer;

        bytes = send(sock_client, response.data(), response.size(), 0);
        close(sock_client);

        if (bytes < 0) {
          std::cerr << "send response failed: " << errno << std::endl;
        }
      });
    }
    close(sock_listener);
  }));

  return 0;
}

int ProviderServer::Register() {
  provider_ = std::unique_ptr<polaris::ProviderApi>(polaris::ProviderApi::CreateWithDefaultFile());
  if (provider_ == nullptr) {
    return -1;
  }
  polaris::InstanceRegisterRequest register_req(service_namespace_, service_name_, service_token_, host_, port_);
  // 开启健康检查
  register_req.SetHealthCheckFlag(true);
  register_req.SetHealthCheckType(polaris::kHeartbeatHealthCheck);
  register_req.SetTtl(kHeartbeatTtl);
  // 实例id不是必填，如果不填，服务端会默认生成一个唯一Id，否则当提供实例id时，需要保证实例id是唯一的
  std::string provided_instance_id = "instance-provided-id";
  register_req.SetInstanceId(provided_instance_id);

  // 注册实例
  auto ret_code = provider_->Register(register_req, instance_id_);
  if (ret_code != polaris::kReturnOk && ret_code != polaris::kReturnExistedResource) {
    std::cout << "register instance with error:" << polaris::ReturnCodeToMsg(ret_code).c_str() << std::endl;
    return ret_code;
  }
  std::cout << "register instance with instance id:" << instance_id_ << std::endl;
  sleep(1);

  // 启动心跳上报线程
  heartbeat_thread_ = std::unique_ptr<std::thread>(new std::thread([=] {
    while (!signal_received) {  // 循环上报心跳
      polaris::InstanceHeartbeatRequest heartbeat_req(service_token_, instance_id_);
      auto ret_code = provider_->Heartbeat(heartbeat_req);
      if (ret_code != polaris::kReturnOk) {
        std::cout << "instance heartbeat with error:" << polaris::ReturnCodeToMsg(ret_code).c_str() << std::endl;
        sleep(1);
        continue;
      }
      sleep(kHeartbeatTtl);
    }
  }));
  return 0;
}

void ProviderServer::Deregister() {
  if (heartbeat_thread_) {
    heartbeat_thread_->join();
  }
  // 反注册实例
  polaris::InstanceDeregisterRequest deregister_req(service_token_, instance_id_);
  auto ret_code = provider_->Deregister(deregister_req);
  if (ret_code != polaris::kReturnOk) {
    std::cout << "instance deregister with error:" << polaris::ReturnCodeToMsg(ret_code).c_str() << std::endl;
  }
}

void ProviderServer::Stop() {
  stop_ = true;
  if (accept_thread_) {
    accept_thread_->join();
    accept_thread_ = nullptr;
  }
}
