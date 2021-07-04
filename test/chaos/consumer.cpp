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

#include "polaris/consumer.h"

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

volatile bool stop = false;

void ReadService(std::vector<polaris::ServiceKey>& services, std::string& time_str) {
  std::ifstream data("services.txt");
  std::string new_time;
  int count;
  int num;
  data >> new_time >> count;
  if (new_time > time_str) {
    time_str = new_time;
    services.clear();
    polaris::ServiceKey new_service;
    std::string token;
    while (count-- > 0) {
      data >> num >> new_service.namespace_ >> new_service.name_ >> token;
      services.push_back(new_service);
    }
  }
  data.close();
}

void* run(void* arg) {
  polaris::ConsumerApi* consumer = reinterpret_cast<polaris::ConsumerApi*>(arg);
  std::vector<polaris::ServiceKey> services;
  std::string time_str = "0";
  polaris::Instance instance;
  polaris::ReturnCode ret;
  uint64_t not_found_count = 0;
  uint64_t ret_err_count   = 0;
  int rand_value           = 0;
  polaris::ServiceKey service_key;
  while (!stop) {
    if (not_found_count % 100000 == 0) {
      ReadService(services, time_str);
      if (services.empty()) {
        sleep(1);
        continue;
      }
      std::cout << "read service count:" << services.size() << std::endl;
      not_found_count = 1;
    }
    rand_value  = rand();
    service_key = services[rand_value % services.size()];
    polaris::GetOneInstanceRequest request(service_key);
    if ((ret = consumer->GetOneInstance(request, instance)) == polaris::kReturnOk) {
      // 上报调用结果
      polaris::ServiceCallResult result;
      result.SetServiceNamespace(service_key.namespace_);
      result.SetServiceName(service_key.name_);
      result.SetInstanceId(instance.GetId());
      result.SetDelay(10 + rand_value % 100);
      result.SetRetCode(0);
      result.SetRetStatus(rand_value % 5 == 0 ? polaris::kCallRetError : polaris::kCallRetOk);
      ret = consumer->UpdateServiceCallResult(result);
    } else {
      if (ret == polaris::kReturnInstanceNotFound || ret == polaris::kReturnServiceNotFound) {
        not_found_count++;
      } else {
        ret_err_count++;
        if (ret_err_count % 1000 == 0) {
          std::cout << "get one instance return " << polaris::ReturnCodeToMsg(ret) << std::endl;
        }
      }
    }
    usleep(10 + rand_value % 100);
  }
  return NULL;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cout << "usage: " << argv[0] << "thread_size run_ms " << std::endl;
    return -1;
  }
  int thread_size = atoi(argv[1]);
  int run_seconds = atoi(argv[2]);
  srand(time(NULL));

  // 创建Consumer对象
  polaris::ConsumerApi* consumer = polaris::ConsumerApi::CreateWithDefaultFile();
  if (consumer == NULL) {
    std::cout << "create consumer api failed" << std::endl;
    return -1;
  }

  std::vector<pthread_t> thread_list;
  for (int i = 0; i < thread_size; i++) {
    pthread_t tid;
    pthread_create(&tid, NULL, run, reinterpret_cast<void*>(consumer));
    thread_list.push_back(tid);
  }

  sleep(run_seconds);
  stop = true;

  for (int i = 0; i < thread_size; i++) {
    pthread_join(thread_list[i], NULL);
  }
  return 0;
}
