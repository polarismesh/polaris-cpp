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
#include <string>
#include <vector>

#include "polaris/consumer.h"

volatile bool stop = false;
polaris::ServiceKey service_key;
bool report = false;
int total;

void *run(void *arg) {
  polaris::ConsumerApi *consumer = reinterpret_cast<polaris::ConsumerApi *>(arg);
  int count                      = 0;
  polaris::GetOneInstanceRequest request(service_key);
  polaris::Instance instance;
  polaris::ReturnCode ret;
  while (!stop) {
    if ((ret = consumer->GetOneInstance(request, instance)) != polaris::kReturnOk) {
      std::cout << "get one instance for service with error:" << ret
                << "msg: " << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
      break;
    }
    count++;

    // 上报调用结果
    if (report) {
      polaris::ServiceCallResult result;
      result.SetServiceNamespace(service_key.namespace_);
      result.SetServiceName(service_key.name_);
      result.SetInstanceId(instance.GetId());
      result.SetDelay(100);
      result.SetRetCode(0);
      result.SetRetStatus(polaris::kCallRetOk);
      if ((ret = consumer->UpdateServiceCallResult(result)) != polaris::kReturnOk) {
        std::cout << "update call result for instance with error:" << ret
                  << " msg:" << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
        break;
      }
    }
  }
  std::cout << count << std::endl;
  __sync_fetch_and_add(&total, count);
  return NULL;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cout << "usage: " << argv[0]
              << " namespace service config_file report_flag thread_size run_ms " << std::endl;
    return -1;
  }
  service_key.namespace_  = argv[1];
  service_key.name_       = argv[2];
  std::string config_file = argv[3];
  std::string report_flag = argv[4];
  report                  = (report_flag == "true" ? true : false);
  int thread_size         = atoi(argv[5]);
  int run_seconds         = atoi(argv[6]);

  // 创建Consumer对象
  polaris::ConsumerApi *consumer = polaris::ConsumerApi::CreateFromFile(config_file);
  if (consumer == NULL) {
    std::cout << "create consumer api failed" << std::endl;
    return -1;
  }

  std::vector<pthread_t> thread_list;
  for (int i = 0; i < thread_size; i++) {
    pthread_t tid;
    pthread_create(&tid, NULL, run, reinterpret_cast<void *>(consumer));
    thread_list.push_back(tid);
  }

  sleep(run_seconds);
  stop = true;

  for (int i = 0; i < thread_size; i++) {
    pthread_join(thread_list[i], NULL);
  }

  std::cout << total / run_seconds << std::endl;

  return 0;
}
