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
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "polaris/polaris_api.h"

int signal_received = 0;
void signal_handler(int signum) {
  printf("interrupt single (%d) received\n", signum);
  signal_received = 1;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    printf("usage: %s service_namespace service_name [interval]\n", argv[0]);
    return -1;
  }
  int interval = argc >= 4 ? atoi(argv[3]) : 1000;

  // 注册信号
  signal(SIGINT, signal_handler);

  // 创建线程安全的polaris api对象
  // 该方法检查当前路径下是否有polaris.yaml文件，如果有则加载该文件配置中的配置项覆盖默认配置，没有则使用默认配置
  polaris_api* polaris_api = polaris_api_new();
  if (polaris_api == NULL) {
    printf("create polaris api failed, see log file ~/polairs/log/polaris.log");
    return -1;
  }

  // 准备请求
  polaris_get_one_instance_req* get_one_instance_req =
      polaris_get_one_instance_req_new(argv[1], argv[2]);
  polaris_instance* instance = NULL;

  // 调用接口
  struct timeval stop, start;
  int ret;
  while (!signal_received) {
    gettimeofday(&start, NULL);
    ret = polaris_api_get_one_instance(polaris_api, get_one_instance_req, &instance);
    if (ret != 0) {
      printf("get instance for service with error %s\n", polaris_get_err_msg(ret));
      sleep(1);
      continue;
    }
    gettimeofday(&stop, NULL);
    printf("get instance, ip: %s, port: %d, use time: %lu us\n",
           polaris_instance_get_host(instance), polaris_instance_get_port(instance),
           (stop.tv_sec - start.tv_sec) * 1000000 + stop.tv_usec - start.tv_usec);
    usleep(interval * 1000);
    polaris_instance_destroy(&instance);
  }
  polaris_get_one_instance_req_destroy(&get_one_instance_req);
  polaris_api_destroy(&polaris_api);
  return 0;
}