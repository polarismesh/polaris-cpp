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

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include <iostream>

#include "polaris/consumer.h"

polaris::ServiceKey g_service_key;
int g_run_times    = 200000;
std::string g_work = "main: ";

void WorkLoop(polaris::ConsumerApi* consumer) {
  for (int i = 0; i < g_run_times; ++i) {
    // 服务发现，准备请求
    polaris::GetOneInstanceRequest request(g_service_key);
    polaris::Instance instance;
    polaris::ReturnCode ret;
    if ((ret = consumer->GetOneInstance(request, instance)) != polaris::kReturnOk) {
      std::cout << "get one instance for service with error: "
                << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
    }
    if (ret != polaris::kReturnOk) {
      sleep(1);
      continue;
    }

    // 业务调用
    std::cout << g_work << "get one instance, ip:" << instance.GetHost()
              << ", port:" << instance.GetPort() << std::endl;
    sleep(1);

    // 上报调用结果
    polaris::ServiceCallResult result;
    result.SetServiceNamespace(g_service_key.namespace_);
    result.SetServiceName(g_service_key.name_);
    result.SetInstanceId(instance.GetId());
    result.SetDelay(100);
    result.SetRetCode(ret);
    result.SetRetStatus(polaris::kCallRetError);
    if ((ret = consumer->UpdateServiceCallResult(result)) != polaris::kReturnOk) {
      std::cout << "update call result for instance with error:" << ret
                << " msg:" << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
    }
  }
}

void* Process(void*) {  // 创建Consumer对象
  polaris::ConsumerApi* consumer = polaris::ConsumerApi::CreateWithDefaultFile();
  if (consumer == NULL) {
    std::cout << "create consumer api failed" << std::endl;
    return NULL;
  }
  WorkLoop(consumer);
  delete consumer;
  return NULL;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cout << "usage: " << argv[0] << " service_namespace service_name [run times]" << std::endl;
    return -1;
  }
  g_service_key.namespace_ = argv[1];
  g_service_key.name_      = argv[2];
  if (argc >= 4) {
    g_run_times = atoi(argv[3]);
  }

  // 创建Consumer对象
  polaris::ConsumerApi* consumer = polaris::ConsumerApi::CreateWithDefaultFile();
  if (consumer == NULL) {
    std::cout << "create consumer api failed" << std::endl;
    return -1;
  }

  if (fork() == 0) {
    g_work = "process: ";
    Process(NULL);
    return 0;
  }

  WorkLoop(consumer);

  delete consumer;
  return 0;
}