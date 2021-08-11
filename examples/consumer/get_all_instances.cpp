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

#include <stdlib.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

#include "polaris/consumer.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cout << "usage: " << argv[0] << " service_namespace service_name" << std::endl;
    return -1;
  }
  polaris::ServiceKey service_key = {argv[1], argv[2]};

  // 创建线程安全的Consumer对象
  // 该方法检查当前路径下是否有polaris.yaml文件
  // 如果有则加载该文件配置中的配置项覆盖默认配置，没有则使用默认配置
  polaris::ConsumerApi* consumer = polaris::ConsumerApi::CreateWithDefaultFile();
  if (consumer == NULL) {
    std::cout << "create consumer api failed" << std::endl;
    return -1;
  }

  polaris::GetInstancesRequest request(service_key);
  polaris::InstancesResponse* response = NULL;

  // 调用接口
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t begin          = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
  polaris::ReturnCode ret = consumer->GetAllInstances(request, response);
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t end = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
  if (ret == polaris::kReturnOk) {
    std::vector<polaris::Instance>& instances = response->GetInstances();
    std::cout << "get all " << instances.size() << " instances, use time:" << end - begin << "us"
              << std::endl;
    for (std::size_t i = 0; i < instances.size(); ++i) {
      std::cout << instances[i].GetHost() << ":" << instances[i].GetPort()
                << ", weight:" << instances[i].GetWeight() << ", "
                << (instances[i].isHealthy() ? "healthy" : "unhealthy") << ", "
                << (instances[i].isIsolate() ? "isolate" : "unisolate")
                << ", region:" << instances[i].GetRegion() << ", zone:" << instances[i].GetZone()
                << ", campus:" << instances[i].GetCampus() << std::endl;
    }
    delete response;
  } else {
    std::cout << "get all instances for service with error:"
              << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
  }

  delete consumer;
  return 0;
}