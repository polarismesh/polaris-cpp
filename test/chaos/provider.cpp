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

#include "polaris/provider.h"

#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

volatile bool stop = false;

struct Service {
  polaris::ServiceKey service_key;
  std::string token;
  std::set<int> ports;
};

void ReadService(std::vector<Service>& services, std::string& time_str) {
  std::ifstream data("services.txt");
  std::string new_time;
  int count;
  int num;
  data >> new_time >> count;
  if (new_time > time_str) {
    time_str = new_time;
    services.clear();
    Service new_service;
    while (count-- > 0) {
      data >> num >> new_service.service_key.namespace_ >> new_service.service_key.name_ >>
          new_service.token;
      services.push_back(new_service);
    }
  }
}

void* run(void* arg) {
  polaris::ProviderApi* provider = reinterpret_cast<polaris::ProviderApi*>(arg);
  std::vector<Service> services;
  std::string time_str = "0";
  polaris::ReturnCode ret;
  int ret_err_count      = 0;
  int deregister_service = 0;
  int select_service     = 0;
  while (!stop) {
    if (ret_err_count % 200 == 0) {
      ReadService(services, time_str);
      if (services.empty()) {
        sleep(1);
        continue;
      }
      std::cout << "read service count:" << services.size() << std::endl;
      ret_err_count      = 1;
      deregister_service = rand() % services.size();
    }
    select_service   = rand() % services.size();
    Service& service = services[select_service % services.size()];
    if (select_service == deregister_service) {  // 所有实例都反注册掉
      for (std::set<int>::iterator it = service.ports.begin(); it != service.ports.end(); ++it) {
        polaris::InstanceDeregisterRequest request(service.service_key.namespace_,
                                                   service.service_key.name_, service.token,
                                                   "127.0.0.1", *it);
        ret = provider->Deregister(request);
      }
      for (int i = 0; i < 200; i++) {
        service.ports.insert(1000 + i);  // 让后续执行以为所有端口都以存在，时反注册失败。
      }
    }
    int select_port = 1000 + rand() % 200;
    if (service.ports.count(select_port) == 0) {  // 注册
      std::string instance_id;
      polaris::InstanceRegisterRequest request(service.service_key.namespace_,
                                               service.service_key.name_, service.token,
                                               "127.0.0.1", select_port);
      ret = provider->Register(request, instance_id);
      if (ret == polaris::kReturnOk) {
        service.ports.insert(select_port);
      } else {
        ret_err_count++;
      }
    } else {  // 反注册
      polaris::InstanceDeregisterRequest request(service.service_key.namespace_,
                                                 service.service_key.name_, service.token,
                                                 "127.0.0.1", select_port);
      ret = provider->Deregister(request);
      if (ret == polaris::kReturnOk) {
        service.ports.erase(select_port);
      } else {
        ret_err_count++;
      }
    }
    usleep(1000 * 1000);
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

  // 创建Consumer对象
  polaris::ProviderApi* provider = polaris::ProviderApi::CreateWithDefaultFile();
  if (provider == NULL) {
    std::cout << "create provider api failed" << std::endl;
    return -1;
  }
  srand(time(NULL));
  std::vector<pthread_t> thread_list;
  for (int i = 0; i < thread_size; i++) {
    pthread_t tid;
    pthread_create(&tid, NULL, run, reinterpret_cast<void*>(provider));
    thread_list.push_back(tid);
  }
  sleep(run_seconds);
  stop = true;

  for (int i = 0; i < thread_size; i++) {
    pthread_join(thread_list[i], NULL);
  }
  return 0;
}
