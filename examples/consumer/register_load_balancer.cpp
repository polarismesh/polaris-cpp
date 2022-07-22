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

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "polaris/consumer.h"
#include "polaris/defs.h"
#include "polaris/log.h"
#include "polaris/model.h"
#include "polaris/plugin.h"

// 定义负载均衡插件类型
static const polaris::LoadBalanceType kLoadBalanceTypeSelfDefine = "kLoadBalanceTypeSelfDefine";

// 定义负载均衡插件，继承LoadBalancer
class SelfDefineLoadBalancer : public polaris::LoadBalancer {
 public:
  SelfDefineLoadBalancer();
  virtual ~SelfDefineLoadBalancer();
  virtual polaris::ReturnCode Init(polaris::Config* config, polaris::Context* context);
  virtual polaris::LoadBalanceType GetLoadBalanceType() { return kLoadBalanceTypeSelfDefine; }
  virtual polaris::ReturnCode ChooseInstance(polaris::ServiceInstances* service_instances,
                                             const polaris::Criteria& criteria, polaris::Instance*& next);
};

SelfDefineLoadBalancer::SelfDefineLoadBalancer() {}

SelfDefineLoadBalancer::~SelfDefineLoadBalancer() {}

polaris::ReturnCode SelfDefineLoadBalancer::Init(polaris::Config*, polaris::Context*) { return polaris::kReturnOk; }

polaris::ReturnCode SelfDefineLoadBalancer::ChooseInstance(polaris::ServiceInstances* service_instances,
                                                           const polaris::Criteria&, polaris::Instance*& next) {
  next = nullptr;
  polaris::InstancesSet* instances_set = service_instances->GetAvailableInstances();
  std::vector<polaris::Instance*> instances = instances_set->GetInstances();
  if (instances.size() > 0) {
    next = instances[0];  // 直接返回第一个实例
    return polaris::kReturnOk;
  }
  return polaris::kReturnInstanceNotFound;
}

// 定义负载均衡插件工厂
polaris::Plugin* SelfDefineLoadBalancerFactory() { return new SelfDefineLoadBalancer(); }

bool signal_received = false;
void SignalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
  signal_received = true;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cout << "usage: " << argv[0] << " service_namespace service_name" << std::endl;
    return -1;
  }
  std::string service_namespace = argv[1];
  std::string service_name = argv[2];

  // 注册信号
  signal(SIGINT, SignalHandler);

  // 设置Logger目录和日志级别
  char temp_dir[] = "/tmp/polaris_log_XXXXXX";
  char* dir_name = mkdtemp(temp_dir);
  std::cout << "set log dir to " << dir_name << std::endl;
  polaris::SetLogDir(dir_name);
  polaris::GetLogger()->SetLogLevel(polaris::kTraceLogLevel);

  // 创建Consumer对象
  polaris::ConsumerApi* consumer = polaris::ConsumerApi::CreateWithDefaultFile();
  if (consumer == nullptr) {
    std::cout << "create consumer api failed" << std::endl;
    return -1;
  }

  // 注册自定义的负载均衡插件
  if (polaris::RegisterPlugin(kLoadBalanceTypeSelfDefine, polaris::kPluginLoadBalancer,
                              SelfDefineLoadBalancerFactory) != polaris::kReturnOk) {
    std::cout << "failed to register plugin" << std::endl;
    return 0;
  }

  // 准备请求
  polaris::ServiceKey service_key = {service_namespace, service_name};
  polaris::GetOneInstanceRequest request(service_key);
  polaris::Instance instance;

  // 使用自定义的负载均衡插件
  request.SetLoadBalanceType(kLoadBalanceTypeSelfDefine);

  // 调用接口
  timespec ts;
  uint64_t begin, end;
  polaris::ReturnCode ret;

  std::map<std::string, int> discover_count;
  while (!signal_received) {
    clock_gettime(CLOCK_REALTIME, &ts);
    begin = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if ((ret = consumer->GetOneInstance(request, instance)) != polaris::kReturnOk) {
      std::cout << "get one instance for service with error: " << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
    }
    if (ret != polaris::kReturnOk) {
      sleep(1);
      continue;
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    end = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    std::cout << "get one instance, ip:" << instance.GetHost() << ", port:" << instance.GetPort()
              << ", use time:" << end - begin << std::endl;

    usleep(1000 * 500);
  }

  delete consumer;
  return 0;
}