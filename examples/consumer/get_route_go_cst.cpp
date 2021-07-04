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

// 本示例用于描述如何使用兼容golang sdk的一致性hash环算法
int main(int argc, char** argv) {
  if (argc < 3) {
    std::cout << "usage: " << argv[0] << " service_namespace service_name" << std::endl;
    return -1;
  }
  polaris::ServiceKey service_key = {argv[1], argv[2]};

  // 此处使用配置字符串配置默认负载均衡算法为一致性hash，并配置兼容go模式
  // 建议将该配置放置在文件中，并通过如下方式创建consumer对象
  // polaris::ConsumerApi* consumer = polaris::ConsumerApi::CreateFromFile(config_file_path);
  std::string config =
      "consumer:\n"
      "  loadBalancer:\n"
      "    type: ringHash\n"
      "    compatibleGo: true";
  polaris::ConsumerApi* consumer = polaris::ConsumerApi::CreateFromString(config);
  if (consumer == NULL) {
    std::cout << "create consumer api failed, see log (default ~/polaris/log/polaris.log)"
              << std::endl;
    return -1;
  }

  for (int i = 0; i < 2000; ++i) {
    // 服务发现请求
    polaris::GetOneInstanceRequest request(service_key);  // 传入被调服务
    request.SetHashString(std::to_string(i));

    polaris::Instance instance;
    polaris::ReturnCode ret;
    // 调用服务发现接口获取服务实例
    if ((ret = consumer->GetOneInstance(request, instance)) != polaris::kReturnOk) {
      std::cout << "get instance for service with error:" << polaris::ReturnCodeToMsg(ret).c_str()
                << std::endl;
      sleep(1);
      continue;
    }

    int rpc_result = 0;
    timespec begin, end;
    clock_gettime(CLOCK_REALTIME, &begin);
    // 使用服务实例进行实际的RPC调用
    // ret_code = RPC_CALL(instance.GetHost(), instance.GetPort());
    std::cout << "key:" << i << " instance id:" << instance.GetId() << std::endl;
    clock_gettime(CLOCK_REALTIME, &end);

    // 上报调用结果
    polaris::ServiceCallResult result;
    result.SetServiceNamespace(service_key.namespace_);
    result.SetServiceName(service_key.name_);
    result.SetInstanceId(instance.GetId());
    int delay = (end.tv_sec - begin.tv_sec) * 1000000 + (end.tv_nsec - begin.tv_nsec) / 1000;
    result.SetDelay(delay);         // 上报延迟
    result.SetRetCode(rpc_result);  // 上报调用返回码
    if (rpc_result >= 0) {  // 这里假设返回码大于0时，rpc调用正常，注意：RPC正常也要上报
      result.SetRetStatus(polaris::kCallRetOk);
    } else {  // rpc调用出错，例如网络错误，超时等上报给北极星用于剔除故障节点
      result.SetRetStatus(polaris::kCallRetError);
    }
    if ((ret = consumer->UpdateServiceCallResult(result)) != polaris::kReturnOk) {
      std::cout << "update call result for instance with error:" << ret
                << " msg:" << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
    }
  }

  delete consumer;
  return 0;
}