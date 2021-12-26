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

#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "polaris/consumer.h"

class ConsumerServer {
public:
  ConsumerServer(const std::string& host, int port, const polaris::ServiceKey& provider_service);

  ~ConsumerServer();

  int Start();

  void Stop();

private:
  std::string Proccess(const std::string& message);

  int Send(const std::string& host, int port, const std::string& request, std::string& response);

private:
  std::string host_;
  int port_;
  polaris::ServiceKey provider_service_;

  std::atomic<bool> stop_;
  std::unique_ptr<std::thread> accept_thread_;

  std::unique_ptr<polaris::ConsumerApi> consumer_;
};

bool signal_received = false;
void SignalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
  signal_received = true;
}

int main(int argc, char** argv) {
  if (argc < 5) {
    std::cout << "usage: " << argv[0] << " host port service_namespace service_name" << std::endl;
    return -1;
  }
  // register signal handler
  signal(SIGINT, SignalHandler);

  polaris::ServiceKey service_key = {argv[3], argv[4]};
  ConsumerServer server(argv[1], atoi(argv[2]), service_key);

  // 启动服务
  if (server.Start() != 0) {
    return -2;
  }

  // 循环等待退出信号
  while (!signal_received) {
    sleep(1);
  }

  // 反注册完成以后再停止服务
  server.Stop();

  return 0;
}

ConsumerServer::ConsumerServer(const std::string& host, int port,
                               const polaris::ServiceKey& provider_service)
    : host_(host), port_(port), provider_service_(provider_service), stop_(false) {
  consumer_ = std::unique_ptr<polaris::ConsumerApi>(polaris::ConsumerApi::CreateWithDefaultFile());
}

ConsumerServer::~ConsumerServer() {}

int ConsumerServer::Start() {
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
  if (bind(sock_listener, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    std::cerr << "bind to " << host_ << ":" << port_ << " failed with error: " << errno
              << std::endl;
    close(sock_listener);
    return -2;
  }

  // start listening
  if (listen(sock_listener, SOMAXCONN) < 0) {
    std::cerr << "listen to " << host_ << ":" << port_ << " failed with error: " << errno
              << std::endl;
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
      timeout.tv_sec  = 2;
      timeout.tv_usec = 0;
      int ret         = select(sock_listener + 1, &set, NULL, NULL, &timeout);
      if (ret <= 0) {
        continue;
      }
      sockaddr_in client_addr;
      socklen_t client_addr_size = sizeof(client_addr);
      int sock_client;
      if ((sock_client = accept(sock_listener, (sockaddr*)&client_addr, &client_addr_size)) < 0) {
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
        std::string response = Proccess(buffer);
        bytes                = send(sock_client, response.data(), response.size(), 0);
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

std::string ConsumerServer::Proccess(const std::string& message) {
  // 获取provider服务实例
  polaris::GetOneInstanceRequest instance_requst(provider_service_);
  polaris::Instance instance;
  auto ret_code = consumer_->GetOneInstance(instance_requst, instance);
  if (ret_code != polaris::kReturnOk) {
    std::cout << "get one instance for service with error: "
              << polaris::ReturnCodeToMsg(ret_code).c_str() << std::endl;
  }

  // 调用业务
  std::string response;
  auto begin_time = std::chrono::steady_clock::now();
  int send_ret    = Send(instance.GetHost(), instance.GetPort(), message, response);
  auto end_time   = std::chrono::steady_clock::now();

  // 上报调用结果
  polaris::ServiceCallResult result;
  result.SetServiceNamespace(provider_service_.namespace_);
  result.SetServiceName(provider_service_.name_);
  result.SetInstanceId(instance.GetId());
  result.SetDelay(
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time - begin_time).count());
  result.SetRetCode(send_ret);
  result.SetRetStatus(send_ret >= 0 ? polaris::kCallRetOk : polaris::kCallRetError);
  if ((ret_code = consumer_->UpdateServiceCallResult(result)) != polaris::kReturnOk) {
    std::cout << "update call result for instance with error:"
              << " msg:" << polaris::ReturnCodeToMsg(ret_code).c_str() << std::endl;
  }

  if (send_ret) {
    response =
        "send msg to " + instance.GetHost() + ":" + std::to_string(instance.GetPort()) + " failed";
  }
  std::cout << response << std::endl;
  return response;
}

int ConsumerServer::Send(const std::string& host, int port, const std::string& request,
                         std::string& response) {
  // create a socket
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    std::cout << "create socket failed: " << errno << std::endl;
    return -1;
  }

  sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
  server_addr.sin_port = htons(port);

  if (connect(sock_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    std::cerr << "connection establish failed: " << errno << std::endl;
    close(sock_fd);
    return -2;
  }

  // send the message
  int bytes_send = send(sock_fd, request.data(), request.length(), 0);
  if (bytes_send < 0) {
    std::cerr << "send message failed: " << errno << std::endl;
    close(sock_fd);
    return -3;
  }

  char buffer[4096];
  int bytes_recv = recv(sock_fd, &buffer, sizeof(buffer), 0);
  if (bytes_recv <= 0) {
    std::cerr << "receive message failed: " << errno << std::endl;
    close(sock_fd);
    return -4;
  }

  close(sock_fd);
  response = std::string(buffer);
  return 0;
}

void ConsumerServer::Stop() {
  stop_ = true;
  if (accept_thread_) {
    accept_thread_->join();
  }
}