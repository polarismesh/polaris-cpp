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
#include <stdlib.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include "polaris/consumer.h"

bool signal_received = false;
void SignalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
  signal_received = true;
}

int main(int argc, char** argv) {
  signal(SIGINT, SignalHandler);  // 注册ctrl+c事件回调，触发进程退出
  if (argc < 3) {
    std::cout << "usage: " << argv[0] << " service_namespace service_name [interval]" << std::endl;
    return -1;
  }
  polaris::ServiceKey service_key = {argv[1], argv[2]};
  int interval = argc >= 4 ? atoi(argv[3]) : 1000;

  // 本示例展示使用北极星SDK进行服务发现的基本步骤

  // 第一步：创建线程安全的Consumer对象
  // 该方法检查当前程序【运行路径】下是否有polaris.yaml文件
  // 如果有则加载该文件配置中的配置项覆盖默认配置，如果没有则使用默认配置
  // 注意：其他创建方法参考头文件"polaris/consumer.h"中ConsumerApi::CreateXXX系列方法注释
  std::string err_msg, content =
                           "global:\n"
                           "  serverConnector:\n"
                           "    addresses:\n"
                           "    - 127.0.0.1:8091\n"                           
                           "consumer:\n"
                           "  healthCheck:\n"
                           "    when: always\n"
                           "    interval: 1s\n"
                           "    chain:\n"
                           "    - http\n"
                           "    plugin:\n"
                           "      http:\n"
                           "        path: /health\n"
                           "  circuitBreaker:\n"
                           "    enable: true\n"
                           "    checkPeriod: 1s";
  polaris::Config* config = polaris::Config::CreateFromString(content, err_msg);
  polaris::Context* context = polaris::Context::Create(config);
  delete config;             // 创建完成后即可释放config对象
  if (context == nullptr) {  // 创建错误，创建失败原因可查看日志~/polaris/log/polaris.log
    abort();
  }
  // 再以共享模式Context创建ConsumerApi，用户自己维护Context的生命周期，该context还可以用于创建ProviderApi
  polaris::ConsumerApi* consumer = polaris::ConsumerApi::Create(context);
  if (consumer == nullptr) {
    std::cout << "create consumer api failed" << std::endl;
    return -1;
  }

  // 【第二步】可选，预拉取服务数据。如果访问的被调服务是已知的，建议加上这个步骤
  polaris::GetOneInstanceRequest request(service_key);
  polaris::Instance instance;
  polaris::ReturnCode ret;
  if ((ret = consumer->InitService(request)) != polaris::kReturnOk) {
    std::cout << "init service with error:" << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
    delete consumer;
    return -1;
  }

  // 调用接口
  timespec ts;
  uint64_t begin, end;
  while (!signal_received) {
    clock_gettime(CLOCK_REALTIME, &ts);
    begin = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

    // 【第三步】RPC调用前调用北极星接口获取一个被调服务实例，会执行服务路由和负载均衡
    if ((ret = consumer->GetOneInstance(request, instance)) != polaris::kReturnOk) {
      std::cout << "get instance for service with error:" << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
      sleep(1);
      continue;
    }

    clock_gettime(CLOCK_REALTIME, &ts);
    end = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    std::cout << "get instance, ip:" << instance.GetHost() << ", port:" << instance.GetPort()
              << ", use time:" << end - begin << "us" << std::endl;

    // 【第四步】使用获取到的服务实例，进行RPC，并获取RPC调用结果返回值和耗时
    int rpc_result = 0;
    clock_gettime(CLOCK_REALTIME, &ts);
    begin = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    // ret_code = RPC_CALL(instance.GetHost(), instance.GetPort());
    clock_gettime(CLOCK_REALTIME, &ts);
    end = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

    // 【第五步】上报使用该被调服务实例进行RPC调用结果
    // 注意：本调用没有网络操作，只将结果写入本地内存
    // 如果RPC是异步的，则异步RPC结束后进行上报即可
    polaris::ServiceCallResult result;
    result.SetServiceNamespace(service_key.namespace_);
    result.SetServiceName(service_key.name_);
    result.SetInstanceId(instance.GetId());
    result.SetDelay(end - begin);   // 上报延迟
    result.SetRetCode(rpc_result);  // 上报调用返回码
    if (rpc_result >= 0) {  // 【注意】这里假设返回码大于0时，rpc调用正常，RPC正常也要上报
      result.SetRetStatus(polaris::kCallRetOk);
    } else {  // rpc调用出错，例如网络错误，超时等上报给北极星用于剔除故障节点
      result.SetRetStatus(polaris::kCallRetError);
    }
    if ((ret = consumer->UpdateServiceCallResult(result)) != polaris::kReturnOk) {
      std::cout << "update call result for instance with error:" << ret
                << " msg:" << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
    }

    usleep(interval * 1000);
  }

  delete consumer;
  return 0;
}