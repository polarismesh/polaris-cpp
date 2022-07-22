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

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <string>

#include "polaris/limit.h"
#include "polaris/log.h"

bool signal_received = false;
void SignalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
  signal_received = true;
}

#define TO_STR(x) static_cast<std::ostringstream&>((std::ostringstream() << std::dec << x)).str()

int main(int argc, char** argv) {
  signal(SIGINT, SignalHandler);  // 注册信号
  if (argc < 4) {
    std::cout << "usage: " << argv[0] << std::endl
              << "    namespace service uin_num" << std::endl
              << "example: " << argv[0] << std::endl
              << "    Test service_name 100" << std::endl;
    return -1;
  }
  polaris::SetLogDir("log");
  // polaris::GetLogger()->SetLogLevel(polaris::kTraceLogLevel);
  std::string service_namespace = argv[1];
  std::string service_name = argv[2];
  int uin_num = atoi(argv[3]);

  // 创建Limit API
  polaris::LimitApi* limit_api = polaris::LimitApi::CreateWithDefaultFile();
  if (limit_api == nullptr) {
    std::cout << "create limit api failed" << std::endl;
    return -1;
  }
  polaris::QuotaRequest quota_request;                   // 限流请求
  quota_request.SetServiceNamespace(service_namespace);  // 设置限流规则对应服务的命名空间
  quota_request.SetServiceName(service_name);            // 设置限流规则对应的服务名
  std::map<std::string, std::string> labels;
  polaris::ReturnCode ret;
  polaris::QuotaResultCode result;
  while (!signal_received) {
    for (int i = 0; i < uin_num; ++i) {
      labels["uin"] = TO_STR(i);
      quota_request.SetLabels(labels);
      if ((ret = limit_api->GetQuota(quota_request, result)) != polaris::kReturnOk) {
        std::cout << "get quota for service with error:" << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
        sleep(1);
        continue;
      }
    }
  }
  delete limit_api;
  return 0;
}