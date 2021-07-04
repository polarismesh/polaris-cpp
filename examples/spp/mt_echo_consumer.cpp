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

/// @file mt_echo_consumer.cpp
/// @brief 微线程单独使用并使用Polaris进行服务发现示例
///

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <vector>

#include "mt_incl.h"

#include "polaris/consumer.h"
#include "polaris/log.h"
#include "polaris/plugin.h"

#define SEND_PKG "spp mt hello world"
#define SEND_PKG_LEN (sizeof(SEND_PKG) - 1)

polaris::ServiceKey service_key;

bool signal_received = false;
void SignalHandler(int signum) { signal_received = true; }

// Task事例类:使用UDP单发单收接口
class UdpSndRcvTask : public IMtTask {
public:
  explicit UdpSndRcvTask(polaris::ConsumerApi* consumer) { consumer_ = consumer; }

  virtual int Process() {
    polaris::GetOneInstanceRequest request(service_key);
    polaris::InstancesResponse* response = NULL;
    polaris::ReturnCode polaris_ret      = consumer_->GetOneInstance(request, response);
    if (polaris_ret != polaris::kReturnOk) {
      printf("get one instance for service with error:%s\n",
             polaris::ReturnCodeToMsg(polaris_ret).c_str());
      return -2;
    }
    polaris::Instance instance = response->GetInstances()[0];
    delete response;

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(instance.GetHost().c_str());
    server_addr.sin_port        = htons(instance.GetPort());

    char buff[1024] = SEND_PKG;
    int max_len     = sizeof(buff);
    uint64_t begin  = mt_time_ms();
    int ret         = mt_udpsendrcv(&server_addr, reinterpret_cast<void*>(buff), SEND_PKG_LEN, buff,
                            max_len, 500);
    if (ret < 0) {
      printf("UdpSndRecvTask mt_udpsendrcv with %s:%d failed, ret %d\n", instance.GetHost().c_str(),
             instance.GetPort(), ret);
    } else {
      printf("UdpSndRcvTask send to %s:%d and recvd: %s\n", instance.GetHost().c_str(),
             instance.GetPort(), buff);
    }

    // 上报调用结果
    polaris::ServiceCallResult result;
    result.SetServiceNamespace(service_key.namespace_);
    result.SetServiceName(service_key.name_);
    result.SetInstanceId(instance.GetId());
    result.SetDelay(mt_time_ms() - begin);
    result.SetRetCode(ret);
    result.SetRetStatus(ret >= 0 ? polaris::kCallRetOk : polaris::kCallRetError);
    if ((polaris_ret = consumer_->UpdateServiceCallResult(result)) != polaris::kReturnOk) {
      printf("update call result for instance with error: %s\n",
             polaris::ReturnCodeToMsg(polaris_ret).c_str());
    }
    return ret;
  }

private:
  polaris::ConsumerApi* consumer_;
};

// 自定义微线程级别的事件通知对象给Polaris使用
class MtDataNotify : public polaris::DataNotify {
public:
  MtDataNotify() { data_loaded_ = false; }

  virtual ~MtDataNotify() {}

  // 通知服务数据加载完成
  virtual void Notify() {
    if (data_loaded_) {
      return;
    }
    data_loaded_ = true;
  }

  // 等待服务数据加载完成
  virtual bool Wait(uint64_t timeout) {
    if (data_loaded_) {
      return true;
    }
    uint64_t expire_ms = mt_time_ms() + timeout;
    do {
      mt_sleep(10);
    } while (!data_loaded_ && mt_time_ms() < expire_ms);
    return data_loaded_;
  }

private:
  bool data_loaded_;
};

// 事件通知对象工厂方法
polaris::DataNotify* MtDataNotifyFactory() { return new MtDataNotify(); }

int main(int argc, char** argv) {
  if (argc < 4) {
    printf("usage: %s service_namespace service_name config_file\n", argv[0]);
    return -1;
  }
  service_key.namespace_ = argv[1];
  service_key.name_      = argv[2];

  // 设置Logger目录和日志级别
  char temp_dir[] = "/tmp/polaris_log_XXXXXX";
  char* dir_name  = mkdtemp(temp_dir);
  printf("set log dir to: %s\n", dir_name);
  polaris::SetLogDir(dir_name);
  polaris::GetLogger()->SetLogLevel(polaris::kTraceLogLevel);

  polaris::ConsumerApi* consumer = polaris::ConsumerApi::CreateFromFile(argv[3]);
  if (consumer == NULL) {
    printf("create consumer api failed");
    return -1;
  }

  // 设置微线程方式的事件监听工厂方法
  if (!polaris::SetDataNotifyFactory(consumer, MtDataNotifyFactory)) {
    fprintf(stderr, "set mt data notify factory for polaris failed.\n");
    return -1;
  }

  // 初始化微线程框架
  if (!mt_init_frame()) {
    fprintf(stderr, "init micro thread frame failed.\n");
    return -1;
  }

  // 触发微线程切换
  mt_sleep(0);

  std::vector<UdpSndRcvTask*> udp_tasks;
  for (int i = 0; i < 10; ++i) {
    udp_tasks.push_back(new UdpSndRcvTask(consumer));
  }

  int ret = 0;
  while (!signal_received) {
    // 这里示例一个并发操作
    IMtTaskList task_list;
    for (std::size_t i = 0; i < udp_tasks.size(); ++i) {
      task_list.push_back(udp_tasks[i]);
    }

    if ((ret = mt_exec_all_task(task_list)) < 0) {
      fprintf(stderr, "execult tasks failed, ret:%d", ret);
      break;
    }

    // 循环检查每一个task是否执行成功，即Process的返回值
    for (unsigned int i = 0; i < task_list.size(); i++) {
      IMtTask* task = task_list[i];
      int result    = task->GetResult();
      if (result < 0) {
        fprintf(stderr, "task(%u) failed, result:%d\n", i, result);
      }
    }
    mt_sleep(1000);
  }
  for (std::size_t i = 0; i < udp_tasks.size(); ++i) {
    delete udp_tasks[i];
  }
  return ret;
}
