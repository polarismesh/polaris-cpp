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
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include "polaris/limit.h"

bool signal_received = false;
void SignalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
  signal_received = true;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cout << "usage: " << argv[0] << std::endl
              << "    service_namespace service_name label1<key:value> label2<key:value> qps"
              << std::endl
              << "example: " << argv[0] << std::endl
              << "    Test service_name labelK1:labelV1 100" << std::endl;
    return -1;
  }
  std::string service_namespace = argv[1];
  std::string service_name      = argv[2];
  std::map<std::string, std::string> labels;
  for (int i = 3; i < argc - 1; ++i) {
    std::string kv  = argv[i];
    std::size_t pos = kv.find(':');
    labels.insert(std::make_pair(kv.substr(0, pos), kv.substr(pos + 1)));
  }
  int qps      = atoi(argv[argc - 1]);
  int interval = 1000 * 1000 / qps;  // 根据传入qps计算每个请求耗时

  // 注册信号
  signal(SIGINT, SignalHandler);

  // 创建Limit API
  polaris::LimitApi* limit_api = polaris::LimitApi::CreateWithDefaultFile();
  if (limit_api == NULL) {
    std::cout << "create limit api failed" << std::endl;
    return -1;
  }

  polaris::QuotaRequest quota_request;                   // 限流请求
  quota_request.SetServiceNamespace(service_namespace);  // 设置限流规则对应服务的命名空间
  quota_request.SetServiceName(service_name);            // 设置限流规则对应的服务名
  quota_request.SetLabels(labels);                       // 设置label用于匹配限流规则

  // 调用接口
  int ok_count       = 0;
  int limit_count    = 0;
  time_t last_second = time(NULL);
  while (!signal_received) {
    polaris::ReturnCode ret;
    polaris::QuotaResponse* response = NULL;
    if ((ret = limit_api->GetQuota(quota_request, response)) != polaris::kReturnOk) {
      std::cout << "get quota for service with error:" << polaris::ReturnCodeToMsg(ret).c_str()
                << std::endl;
      sleep(1);
      continue;
    }
    polaris::QuotaResultCode result = response->GetResultCode();
    delete response;
    if (result == polaris::kQuotaResultOk) {
      // 请求未被限流，将usleep替换成真正的业务请求
      usleep(interval);
      ok_count++;
    } else {
      // 请求被限流，将usleep替换成请求拒绝逻辑
      usleep(interval);
      limit_count++;
    }
    time_t current_second = time(NULL);
    if (current_second >= last_second + 1) {
      std::cout << "time:" << last_second << " ok:" << ok_count << " limited:" << limit_count
                << std::endl;
      last_second = current_second;
      ok_count    = 0;
      limit_count = 0;
    }
  }

  delete limit_api;  // 程序退出前 释放limit api对象
  return 0;
}