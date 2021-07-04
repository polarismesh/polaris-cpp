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

#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "polaris/consumer.h"
#include "polaris/log.h"

bool signal_received = false;
void SignalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
  signal_received = true;
}

polaris::ReturnCode Send(const std::string& host, int port, const std::string& data) {
  int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd < 0) {
    std::cout << "create socket error:" << errno << " msg:" << strerror(errno) << std::endl;
    return polaris::kReturnNetworkFailed;
  }
  struct sockaddr_in dst_addr;
  dst_addr.sin_family      = AF_INET;
  dst_addr.sin_port        = htons(port);
  dst_addr.sin_addr.s_addr = inet_addr(host.c_str());
  struct timeval timeout_val;
  timeout_val.tv_sec  = 1;
  timeout_val.tv_usec = 0;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (const void*)&timeout_val,
                 sizeof(timeout_val)) < 0) {
    std::cout << "setsockopt SO_SNDTIMEO error:" << errno << " msg:" << strerror(errno)
              << std::endl;
    close(socket_fd);
    return polaris::kReturnNetworkFailed;
  }
  ssize_t bytes = sendto(socket_fd, data.c_str(), data.size(), 0, (struct sockaddr*)&dst_addr,
                         sizeof(dst_addr));
  if (bytes < 0) {
    std::cout << "send failed to " << host.c_str() << ":" << port << ",  errno:" << errno
              << ", errmsg:" << strerror(errno) << std::endl;
    close(socket_fd);
    return errno == EAGAIN ? polaris::kReturnTimeout : polaris::kReturnNetworkFailed;
  } else {
    std::cout << "send to " << host << ":" << port << ", data:" << data << std::endl;
  }
  if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const void*)&timeout_val,
                 sizeof(timeout_val)) < 0) {
    std::cout << "setsockopt SO_RCVTIMEO error:" << errno << " msg:" << strerror(errno)
              << std::endl;
    close(socket_fd);
    return polaris::kReturnNetworkFailed;
  }
  char buffer[1024];
  memset(buffer, 0, sizeof(buffer));
  if ((bytes = recvfrom(socket_fd, buffer, sizeof(buffer), 0, NULL, NULL)) < 0) {
    std::cout << "recv failed from " << host.c_str() << ":" << port << ",  errno:" << errno
              << ", errmsg:" << strerror(errno) << std::endl;
    close(socket_fd);
    return errno == EAGAIN ? polaris::kReturnTimeout : polaris::kReturnNetworkFailed;
  } else {
    std::cout << "recv from " << host << ":" << port << ", data:" << buffer << std::endl;
  }
  close(socket_fd);
  return polaris::kReturnOk;
}

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cout << "usage: " << argv[0] << " service_namespace service_name [sync/async]"
              << std::endl;
    return -1;
  }
  std::string service_namespace = argv[1];
  std::string service_name      = argv[2];
  std::string async_flag;
  if (argc >= 4) {
    async_flag = argv[3];
  }

  // 注册信号
  signal(SIGINT, SignalHandler);

  // 设置Logger目录和日志级别
  char temp_dir[] = "/tmp/polaris_log_XXXXXX";
  char* dir_name  = mkdtemp(temp_dir);
  std::cout << "set log dir to " << dir_name << std::endl;
  polaris::SetLogDir(dir_name);
  polaris::GetLogger()->SetLogLevel(polaris::kTraceLogLevel);

  // 创建Consumer对象
  polaris::ConsumerApi* consumer = polaris::ConsumerApi::CreateWithDefaultFile();
  if (consumer == NULL) {
    std::cout << "create consumer api failed" << std::endl;
    return -1;
  }

  // 准备请求
  polaris::ServiceKey service_key = {service_namespace, service_name};
  polaris::GetOneInstanceRequest request(service_key);
  polaris::Instance instance;
  // 调用接口
  timespec ts;
  uint64_t begin, end;
  polaris::ReturnCode ret;

  std::map<std::string, int> discover_count;
  while (!signal_received) {
    clock_gettime(CLOCK_REALTIME, &ts);
    begin = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (async_flag != "async") {
      if ((ret = consumer->GetOneInstance(request, instance)) != polaris::kReturnOk) {
        std::cout << "get one instance for service with error: "
                  << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
      }
    } else {
      polaris::InstancesFuture* future = NULL;
      if ((ret = consumer->AsyncGetOneInstance(request, future)) != polaris::kReturnOk) {
        std::cout << "get one instance future for service with error: "
                  << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
      } else {
        polaris::InstancesResponse* response = NULL;
        if ((ret = future->Get(1000, response)) != polaris::kReturnOk) {
          std::cout << "wait one instance future for service with error: "
                    << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
        } else {
          instance = response->GetInstances()[0];
        }
        delete response;
        delete future;
      }
    }
    if (ret != polaris::kReturnOk) {
      sleep(1);
      continue;
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    end = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    std::cout << "get one instance, ip:" << instance.GetHost() << ", port:" << instance.GetPort()
              << ", use time:" << end - begin << std::endl;

    // 调用业务
    std::map<std::string, int>::iterator discover_it = discover_count.find(instance.GetId());
    if (discover_it == discover_count.end()) {
      discover_count.insert(std::make_pair(instance.GetId(), 0));
      discover_it = discover_count.find(instance.GetId());
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    begin = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;  // 以微妙计数
    std::stringstream ss;
    ss << "send request count:" << discover_it->second++;
    ret = Send(instance.GetHost(), instance.GetPort(), ss.str());
    clock_gettime(CLOCK_REALTIME, &ts);
    end = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

    // 上报调用结果
    polaris::ServiceCallResult result;
    result.SetServiceNamespace(service_namespace);
    result.SetServiceName(service_name);
    result.SetInstanceId(instance.GetId());
    result.SetDelay(end - begin);
    if (ret == polaris::kReturnOk) {
      result.SetRetCode(0);
      result.SetRetStatus(polaris::kCallRetOk);
    } else {
      result.SetRetCode(ret);
      result.SetRetStatus(ret == polaris::kReturnTimeout ? polaris::kCallRetTimeout
                                                         : polaris::kCallRetError);
    }
    if ((ret = consumer->UpdateServiceCallResult(result)) != polaris::kReturnOk) {
      std::cout << "update call result for instance with error:" << ret
                << " msg:" << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
    }
    usleep(200 * 1000);
  }

  delete consumer;
  return 0;
}