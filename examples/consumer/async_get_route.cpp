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

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include "polaris/consumer.h"
#include "polaris/log.h"

bool signal_received = false;
void SignalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
  signal_received = true;
}

class RequestCallback : public polaris::ServiceCacheNotify {
 public:
  RequestCallback(polaris::InstancesFuture* future, int request_id) : future_(future), request_id_(request_id) {}
  virtual ~RequestCallback() { delete future_; }

  virtual void NotifyReady() {
    polaris::InstancesResponse* resp = nullptr;
    polaris::ReturnCode ret_code = future_->Get(0, resp);
    if (ret_code == polaris::kReturnOk) {
      polaris::Instance& instance = resp->GetInstances()[0];
      std::cout << "callback get instance, ip:" << instance.GetHost() << ", port:" << instance.GetPort()
                << "  for request " << request_id_ << std::endl;
      delete resp;
    } else {
      std::cout << "request id " << request_id_ << " get instance with error "
                << polaris::ReturnCodeToMsg(ret_code).c_str() << std::endl;
    }
  }

  virtual void NotifyTimeout() { std::cout << "request id " << request_id_ << " get instance timeout" << std::endl; }

 private:
  polaris::InstancesFuture* future_;
  int request_id_;
};

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cout << "usage: " << argv[0] << " service_namespace service_name" << std::endl;
    return -1;
  }
  polaris::ServiceKey service_key = {argv[1], argv[2]};

  // 注册信号
  signal(SIGINT, SignalHandler);

  polaris::GetLogger()->SetLogLevel(polaris::kTraceLogLevel);
  polaris::SetLogDir("./log");

  // 创建线程安全的Consumer对象
  // 该方法检查当前路径下是否有polaris.yaml文件，如果有则加载该文件配置中的配置项覆盖默认配置，没有则使用默认配置
  polaris::ConsumerApi* consumer = polaris::ConsumerApi::CreateWithDefaultFile();
  if (consumer == nullptr) {
    std::cout << "create consumer api failed" << std::endl;
    return -1;
  }

  // 准备请求
  polaris::GetOneInstanceRequest request(service_key);
  // 调用接口
  polaris::ReturnCode ret;
  int count = 0;
  while (!signal_received) {
    polaris::InstancesFuture* future = nullptr;
    if ((ret = consumer->AsyncGetOneInstance(request, future)) != polaris::kReturnOk) {
      std::cout << "async get instance for service with error " << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
      sleep(1);
      continue;
    }
    if (future->IsDone(false)) {
      polaris::InstancesResponse* resp = nullptr;
      ret = future->Get(0, resp);
      if (ret == polaris::kReturnOk) {
        polaris::Instance& instance = resp->GetInstances()[0];
        std::cout << "get instance, ip:" << instance.GetHost() << ", port:" << instance.GetPort() << " for request "
                  << count++ << std::endl;
      } else {
        std::cout << "future get instance for service with error " << polaris::ReturnCodeToMsg(ret).c_str()
                  << std::endl;
      }
      delete future;
      delete resp;
    } else {
      future->SetServiceCacheNotify(new RequestCallback(future, count++));
    }
    sleep(1);
  }

  delete consumer;
  return 0;
}