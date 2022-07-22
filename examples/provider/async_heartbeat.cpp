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

class HeartbeatCallback : public polaris::ProviderCallback {
 public:
  HeartbeatCallback(std::string host, int port) : host_(host), port_(port) {}

  ~HeartbeatCallback() {}

  virtual void Response(polaris::ReturnCode code, const std::string& message) {
    std::cout << "async heartbeat for " << host_ << ":" << port_ << " code:" << polaris::ReturnCodeToMsg(code).c_str()
              << " message:" << message << std::endl;
  }

 private:
  std::string host_;
  int port_;
};

int main(int argc, char** argv) {
  if (argc < 6) {
    std::cout << "usage: " << argv[0] << " service_namespace service_name service_token host port" << std::endl;
    return -1;
  }
  std::string service_namespace = argv[1];
  std::string service_name = argv[2];
  std::string service_token = argv[3];
  std::string host = argv[4];
  int port = atoi(argv[5]);

  // 注册信号
  signal(SIGINT, SignalHandler);

  // 设置Logger目录和日志级别
  polaris::GetLogger()->SetLogLevel(polaris::kTraceLogLevel);

  // 创建Provider API
  polaris::ProviderApi* provider = polaris::ProviderApi::CreateWithDefaultFile();
  if (provider == nullptr) {
    std::cout << "create provider api failed" << std::endl;
    return -1;
  }

  sleep(2);
  // 等待服务启动成功后再去注册服务
  polaris::InstanceRegisterRequest register_req(service_namespace, service_name, service_token, host, port);
  register_req.SetHealthCheckFlag(true);
  register_req.SetHealthCheckType(polaris::kHeartbeatHealthCheck);
  register_req.SetTtl(5);  // 5s 未上报心跳就超时

  polaris::ReturnCode ret;
  std::string instance_id;
  // 调用注册接口
  ret = provider->Register(register_req, instance_id);
  if (ret != polaris::kReturnOk && ret != polaris::kReturnExistedResource) {
    std::cout << "register instance with error code:" << ret << " msg:" << polaris::ReturnCodeToMsg(ret).c_str()
              << std::endl;
    abort();
  }
  std::cout << "register instance return id:" << instance_id << std::endl;

  sleep(2);

  // 循环上报心跳
  polaris::InstanceHeartbeatRequest heartbeat_req(service_token, instance_id);
  heartbeat_req.SetTimeout(1000);
  while (!signal_received) {
    HeartbeatCallback* callback = new HeartbeatCallback(host, port);
    ret = provider->AsyncHeartbeat(heartbeat_req, callback);
    if (ret != polaris::kReturnOk) {
      std::cout << "async heartbeat with error code:" << ret << " msg:" << polaris::ReturnCodeToMsg(ret).c_str()
                << std::endl;
      sleep(1);
      continue;
    }
    sleep(5);
  }

  // 反注册服务实例
  polaris::InstanceDeregisterRequest deregister_req(service_token, instance_id);
  ret = provider->Deregister(deregister_req);
  if (ret == polaris::kReturnOk) {
    std::cout << "deregister instance success" << std::endl;
  } else {
    std::cout << "instance deregister with error code:" << ret << " msg:" << polaris::ReturnCodeToMsg(ret).c_str()
              << std::endl;
  }

  delete provider;
  return 0;
}