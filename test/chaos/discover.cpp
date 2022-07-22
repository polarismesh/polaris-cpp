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

#include <unistd.h>

#include "discover.h"

namespace polaris {

static const std::string kInstanceHost = "127.0.0.1";

DiscoverChaos::DiscoverChaos()
    : last_deregister_port_(0),
      instance_num_(50),
      provider_(nullptr),
      consumer_(nullptr),
      timing_consumer_(nullptr),
      idle_consumer_(nullptr) {}

bool DiscoverChaos::Init(Config* config) {
  Config* discover = config->GetSubConfig("discover");
  service_key_.namespace_ = discover->GetStringOrDefault("namespace", "Test");
  service_key_.name_ = discover->GetStringOrDefault("service", "");
  token_ = discover->GetStringOrDefault("token", "");
  delete discover;
  if (service_key_.name_.empty()) {
    CHAOS_INFO("get service name failed");
    return false;
  }
  if (token_.empty()) {
    CHAOS_INFO("get service token failed");
    return false;
  }
  return true;
}

bool DiscoverChaos::SetUp() {
  provider_ = ProviderApi::CreateWithDefaultFile();
  if (provider_ == nullptr) {
    CHAOS_INFO("create provider failed");
    return false;
  }
  consumer_ = ConsumerApi::CreateWithDefaultFile();
  if (consumer_ == nullptr) {
    CHAOS_INFO("create consumer failed");
    return false;
  }
  timing_consumer_ = ConsumerApi::CreateWithDefaultFile();
  if (timing_consumer_ == nullptr) {
    CHAOS_INFO("create timing consumer failed");
    return false;
  }
  idle_consumer_ = ConsumerApi::CreateWithDefaultFile();
  if (idle_consumer_ == nullptr) {
    CHAOS_INFO("create idle consumer failed");
    return false;
  }
  if (!PrepareData()) {
    return false;
  }
  return true;
}

void DiscoverChaos::TearDown() {
  if (provider_ != nullptr) {
    delete provider_;
    provider_ = nullptr;
  }
  if (consumer_ != nullptr) {
    delete consumer_;
    consumer_ = nullptr;
  }
  if (timing_consumer_ != nullptr) {
    delete timing_consumer_;
    timing_consumer_ = nullptr;
  }
  if (idle_consumer_ != nullptr) {
    delete idle_consumer_;
    idle_consumer_ = nullptr;
  }
}

void DiscoverChaos::Run() {
  // 先获取当前实例信息
  ReturnCode ret_code = kReturnOk;
  int last_timing_task = 0;

  int loop_count = 0;
  CHAOS_INFO("begin run loop");
  while (!stop_received_) {
    switch (loop_count++ % 20) {
      case 0:  // 注册实例
      {
        std::string instance_id;
        int port = SelectNextPort();
        InstanceRegisterRequest request(service_key_.namespace_, service_key_.name_, token_, kInstanceHost, port);
        request.SetTimeout(2000);
        request.SetHealthCheckFlag(true);
        request.SetTtl(5);
        ret_code = provider_->Register(request, instance_id);
        if (ret_code == kReturnOk || ret_code == kReturnExistedResource) {
          port_set.insert(port);
        } else {
          CHAOS_ERROR("register instance with port  %d to service %s return %s", port, service_key_.name_.c_str(),
                      ReturnCodeToMsg(ret_code).c_str());
        }
      } break;
      case 10:  // 进行服务注册
        if (!token_.empty() && !port_set.empty()) {
          int port = *port_set.begin();
          InstanceDeregisterRequest request(service_key_.namespace_, service_key_.name_, token_, "127.0.0.1", port);
          request.SetTimeout(2000);
          if ((ret_code = provider_->Deregister(request)) == kReturnOk) {
            port_set.erase(port);
            last_deregister_port_ = port;
          } else {
            CHAOS_ERROR("deregister instance with port  %d to service %s return %s", port, service_key_.name_.c_str(),
                        ReturnCodeToMsg(ret_code).c_str());
          }
        }
        break;
      default:  // 进行服务发现
        GetOneInstanceRequest request(service_key_);
        InstancesResponse* resp = nullptr;
        if ((ret_code = consumer_->GetOneInstance(request, resp)) == kReturnOk) {
          Instance& instance = resp->GetInstances()[0];
          ServiceCallResult result;
          result.SetServiceNamespace(service_key_.namespace_);
          result.SetServiceName(service_key_.name_);
          result.SetInstanceId(instance.GetId());
          result.SetDelay(rand() % 100);
          result.SetRetCode(0);
          result.SetRetStatus(rand() % 5 == 0 ? kCallRetError : kCallRetOk);
          consumer_->UpdateServiceCallResult(result);
          int port = instance.GetPort();
          delete resp;
          if (!token_.empty() && port_set.find(port) == port_set.end() && port != last_deregister_port_) {
            CHAOS_ERROR("discover instance but service[%s] port[%d] is deregister", service_key_.name_.c_str(), port);
            return;
          }
        }
        break;
    }
    sleep(1);

    // 24小时定时任务
    if (last_timing_task + 24 * 60 * 60 < time(nullptr)) {
      GetOneInstanceRequest request(service_key_);
      InstancesResponse* resp = nullptr;
      if ((ret_code = timing_consumer_->GetOneInstance(request, resp)) != kReturnOk) {
        CHAOS_ERROR("timing discover service[%s] return %s", service_key_.name_.c_str(),
                    ReturnCodeToMsg(ret_code).c_str());
      } else {
        delete resp;
      }
      last_timing_task = time(nullptr);
    }
  }
  CHAOS_INFO("exit loop");
}

int DiscoverChaos::SelectNextPort() {
  if (!port_set.empty()) {
    if (*port_set.rbegin() < 10000) {
      return *port_set.rbegin() + 1;
    } else {
      for (int port = 80; port < 10000; ++port) {
        if (port_set.count(port) == 0) {
          return port;
        }
      }
      return 10001;
    }
    return (*port_set.rbegin() + 1) % 10000 + 1;
  } else {
    return 80;
  }
}

bool DiscoverChaos::PrepareData() {
  ReturnCode ret_code = kReturnOk;
  GetInstancesRequest discover_request(service_key_);
  InstancesResponse* respone = nullptr;
  if ((ret_code = idle_consumer_->GetAllInstances(discover_request, respone)) != kReturnOk) {
    CHAOS_INFO("get all instance for %s with error %s", service_key_.name_.c_str(), ReturnCodeToMsg(ret_code).c_str());
    return false;
  }
  std::vector<Instance>& instances = respone->GetInstances();
  for (std::size_t j = 0; j < instances.size(); ++j) {
    Instance& instance = instances[j];
    port_set.insert(instance.GetPort());
    CHAOS_INFO("load instance with port: %d", instance.GetPort());
  }
  CHAOS_INFO("load %zu instance", port_set.size());

  // 该把实例全部注册一遍
  int register_count = instance_num_ - port_set.size();
  while (port_set.size() < instance_num_) {
    int port = SelectNextPort();
    InstanceRegisterRequest request(service_key_.namespace_, service_key_.name_, token_, kInstanceHost, port);
    request.SetTimeout(2000);
    std::string instance_id;
    ret_code = provider_->Register(request, instance_id);
    if (ret_code != kReturnOk && ret_code != kReturnExistedResource) {
      CHAOS_ERROR("register instance with port %d to service %s with error %s", port, service_key_.name_.c_str(),
                  ReturnCodeToMsg(ret_code).c_str());
      return false;
    }
    port_set.insert(port);
  }
  CHAOS_INFO("register %d instance", register_count);
  return true;
}

}  // namespace polaris
