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
#include <string>

#include "polaris/limit.h"
#include "polaris/log.h"

bool signal_received = false;
void SignalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
  signal_received = true;
}

struct ThreadArgs {
  polaris::QuotaRequest request_;
  pthread_t tid_;
  int begin_wait_;
  int interval_;
  volatile int* ok_count_;
};

void* ThreadFunc(void* args) {
  ThreadArgs* thread_args = static_cast<ThreadArgs*>(args);
  // 创建Limit API
  polaris::LimitApi* limit_api = polaris::LimitApi::CreateWithDefaultFile();
  if (limit_api == nullptr) {
    std::cout << "create limit api failed" << std::endl;
    return nullptr;
  }
  usleep(thread_args->begin_wait_);
  while (!signal_received) {
    polaris::ReturnCode ret;
    polaris::QuotaResponse* response = nullptr;
    if ((ret = limit_api->GetQuota(thread_args->request_, response)) != polaris::kReturnOk) {
      std::cout << "get quota for service with error:" << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
      sleep(1);
      continue;
    }
    polaris::QuotaResultCode result = response->GetResultCode();
    delete response;
    if (result == polaris::kQuotaResultOk) {
      __sync_fetch_and_add(thread_args->ok_count_, 1);
    }
    usleep(thread_args->interval_);
  }

  delete limit_api;  // 程序退出前 释放limit api对象
  return nullptr;
}

int main(int argc, char** argv) {
  signal(SIGINT, SignalHandler);  // 注册信号
  if (argc < 7) {
    std::cout << "usage: " << argv[0] << std::endl
              << "    namespace service label1<key:value> test_qps limit_qps thread_num" << std::endl
              << "example: " << argv[0] << std::endl
              << "    Test service_name labelK1:labelV1 1000 100" << std::endl;
    return -1;
  }
  polaris::SetLogDir("log");
  // polaris::GetLogger()->SetLogLevel(polaris::kTraceLogLevel);
  std::string service_namespace = argv[1];
  std::string service_name = argv[2];
  std::map<std::string, std::string> labels;
  std::string kv = argv[3];
  std::size_t pos = kv.find(':');
  labels.insert(std::make_pair(kv.substr(0, pos), kv.substr(pos + 1)));
  int test_qps = atoi(argv[4]);
  int limit_qps = atoi(argv[5]);

  int thread_num = atoi(argv[6]);
  std::vector<ThreadArgs*> thread_args;
  volatile int ok_count = 0;
  for (int i = 0; i < thread_num; ++i) {
    ThreadArgs* args = new ThreadArgs();
    args->request_.SetServiceNamespace(service_namespace);  // 设置限流规则对应服务的命名空间
    args->request_.SetServiceName(service_name);            // 设置限流规则对应的服务名
    args->request_.SetLabels(labels);                       // 设置label用于匹配限流规则
    args->begin_wait_ = i * 100 * 1000;
    args->interval_ = (1000 * 1000 - args->begin_wait_) / test_qps;  // 根据传入qps计算每个请求耗时
    args->ok_count_ = &ok_count;
    if (pthread_create(&args->tid_, nullptr, ThreadFunc, args) < 0) {
      std::cout << "create thread failed" << std::endl;
      return -1;
    }
    thread_args.push_back(args);
  }

  time_t last_second = time(nullptr);
  int last_ok = 0;
  int interval = 1000 * 1000 / test_qps;  // 根据传入qps计算每个请求耗时
  while (!signal_received) {
    usleep(interval);
    time_t current_second = time(nullptr);
    if (current_second >= last_second + 1) {
      int current_ok = __sync_add_and_fetch(&ok_count, 0);
      std::cout << "time:" << last_second << " ok:" << current_ok - last_ok
                << " diff:" << current_ok - last_ok - limit_qps
                << " rate:" << (current_ok - last_ok - limit_qps) * 1.0 / limit_qps << std::endl;
      last_second = current_second;
      last_ok = current_ok;
    }
  }

  for (std::size_t i = 0; i < thread_args.size(); ++i) {
    ThreadArgs* args = thread_args[i];
    if (pthread_join(args->tid_, nullptr) < 0) {
      std::cout << "join thread failed" << std::endl;
      return -1;
    }
  }
  return 0;
}