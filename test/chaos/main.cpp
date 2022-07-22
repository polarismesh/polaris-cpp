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

#include <iostream>

#include "discover.h"
#include "heartbeat.h"
#include "instance_not_exist.h"
#include "service_not_exist.h"

#include "polaris/polaris.h"

bool signal_received = false;
void SignalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
  signal_received = true;
}

int main(int, char**) {
  signal(SIGINT, SignalHandler);  // 注册信号

  polaris::SetLogDir("./log");

  // 创建用例
  polaris::ServiceNotExist service_not_exist;
  polaris::InstanceNotExist instance_not_exist;

  std::string err_msg;
  polaris::Config* config = polaris::Config::CreateFromFile("./chaos.yaml", err_msg);
  if (config == nullptr) {
    std::cout << "load config file ./chaos.yaml with error: " << err_msg << std::endl;
    return -1;
  }
  polaris::DiscoverChaos discover_chaos;
  polaris::HeartbeatChaos heartbat_chaos;

  bool init = discover_chaos.Init(config) && heartbat_chaos.Init(config);
  delete config;
  if (!init) {
    return -1;
  }
  std::cout << "init all chaos success" << std::endl;

  // 启动用例运行
  if (service_not_exist.Start() && instance_not_exist.Start() && discover_chaos.Start() && heartbat_chaos.Start()) {
    while (!signal_received) {
      sleep(1);
    }
  }

  std::cout << "begin stop all chaos" << std::endl;
  service_not_exist.Stop();
  instance_not_exist.Stop();
  discover_chaos.Stop();
  heartbat_chaos.Stop();

  return 0;
}
