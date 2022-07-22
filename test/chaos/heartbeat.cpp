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

#include "heartbeat.h"

namespace polaris {

static const std::string kInstanceHost = "127.0.0.1";

static const int kNormalPort = 8080;           // 正常上报实例
static const int kNoHeartbeatPort = 8081;      // 不进行心跳上报实例
static const int kSleepHeartbeatPort = 8082;   // 一会上报一会不上报
static const int kRandomHeartbeatPort = 8083;  // 随机间隔进行心跳上报

static const int kErrorTokenPort = 8084;        // 错误的token
static const int kDisableHeartbeatPort = 8085;  // 禁止心跳上报
static const int kNotRegisterPort = 8086;       // 未注册实例

static const int kHeartbeatTtL = 5;

HeartbeatChaos::HeartbeatChaos() : provider_(nullptr), consumer_(nullptr) {}

bool HeartbeatChaos::Init(Config* config) {
  Config* heartbeat = config->GetSubConfig("heartbeat");
  service_key_.namespace_ = heartbeat->GetStringOrDefault("namespace", "Test");
  service_key_.name_ = heartbeat->GetStringOrDefault("service", "");
  token_ = heartbeat->GetStringOrDefault("token", "");
  delete heartbeat;

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

bool HeartbeatChaos::SetUp() {
  if ((provider_ = ProviderApi::CreateWithDefaultFile()) == nullptr) {
    CHAOS_INFO("crate provider api failed");
    return false;
  }
  if ((consumer_ = ConsumerApi::CreateWithDefaultFile()) == nullptr) {
    CHAOS_INFO("crate consumer api failed");
    return false;
  }
  // 注册所有实例
  for (int port = kNormalPort; port < kNotRegisterPort; ++port) {
    InstanceRegisterRequest request(service_key_.namespace_, service_key_.name_, token_, kInstanceHost, port);
    request.SetTimeout(2000);
    if (port < kDisableHeartbeatPort) {
      request.SetHealthCheckFlag(true);
      request.SetTtl(kHeartbeatTtL);
    }
    std::string instance_id;
    ReturnCode ret_code = provider_->Register(request, instance_id);
    if (ret_code != kReturnOk && ret_code != kReturnExistedResource) {
      CHAOS_INFO("register instance with port %d to service %s with error %s", port, service_key_.name_.c_str(),
                 ReturnCodeToMsg(ret_code).c_str());
      return false;
    }
    if (port == kNormalPort) {
      normal_instance_id_ = instance_id;
    }
  }

  return true;
}

void HeartbeatChaos::Run() {
  ReturnCode ret_code;
  int normal_report_time = 0;
  int no_heartbeat_time = time(nullptr) + 5 * 60;
  bool no_heartbeat_register = true;
  int sleep_report_time = 0;
  int random_report_time = 0;

  int discover_time = time(nullptr) + 10;

  int disable_report_time = 0;
  int error_token_report_time = 0;
  int not_register_report_time = 0;
  CHAOS_INFO("begin run loop");
  while (!stop_received_) {
    // 正常上报的实例
    if (normal_report_time < time(nullptr)) {
      InstanceHeartbeatRequest request(token_, normal_instance_id_);
      ret_code = provider_->Heartbeat(request);
      if (ret_code == kReturnOk) {
        normal_report_time = time(nullptr) + kHeartbeatTtL;
      }
    }

    // 不进行心跳上报实例，5分钟进行一次注册or反注册操作
    if (no_heartbeat_time < time(nullptr)) {
      if (no_heartbeat_register) {
        InstanceDeregisterRequest request(service_key_.namespace_, service_key_.name_, token_, kInstanceHost,
                                          kNoHeartbeatPort);
        if ((ret_code = provider_->Deregister(request)) == kReturnOk) {
          no_heartbeat_register = false;
          no_heartbeat_time = time(nullptr) + 5 * 60;
        }
      } else {
        InstanceRegisterRequest request(service_key_.namespace_, service_key_.name_, token_, kInstanceHost,
                                        kNoHeartbeatPort);
        request.SetHealthCheckFlag(true);
        request.SetTtl(kHeartbeatTtL);
        std::string instance_id;
        ReturnCode ret_code = provider_->Register(request, instance_id);
        if (ret_code == kReturnOk || ret_code == kReturnExistedResource) {
          no_heartbeat_register = true;
          no_heartbeat_time = time(nullptr) + 5 * 60;
        }
      }
    }

    // 1分钟上报，1分钟不上报
    if (sleep_report_time < time(nullptr)) {
      bool report = (time(nullptr) / 60) % 2;
      if (report) {
        InstanceHeartbeatRequest request(service_key_.namespace_, service_key_.name_, token_, kInstanceHost,
                                         kSleepHeartbeatPort);
        ret_code = provider_->Heartbeat(request);
        if (ret_code == kReturnOk) {
          sleep_report_time = time(nullptr) + kHeartbeatTtL;
        }
      } else {
        sleep_report_time = time(nullptr) + kHeartbeatTtL;
      }
    }

    // 随机上报间隔
    if (random_report_time < time(nullptr)) {
      InstanceHeartbeatRequest request(service_key_.namespace_, service_key_.name_, token_, kInstanceHost,
                                       kRandomHeartbeatPort);
      ret_code = provider_->Heartbeat(request);
      if (ret_code == kReturnOk) {
        random_report_time = time(nullptr) + rand() % kHeartbeatTtL + kHeartbeatTtL / 2 + 1;
      }
    }

    // 进行服务发现，检查实例状态
    while (discover_time < time(nullptr)) {
      GetInstancesRequest request(service_key_);
      InstancesResponse* resp = nullptr;
      if ((ret_code = consumer_->GetAllInstances(request, resp)) != kReturnOk) {
        CHAOS_ERROR("%s heartbeat get all instance with error: %s", service_key_.name_.c_str(),
                    ReturnCodeToMsg(ret_code).c_str());
        discover_time = time(nullptr) + 1;
        break;
      }
      std::vector<Instance>& instances = resp->GetInstances();
      for (std::size_t i = 0; i < instances.size(); ++i) {
        Instance& instance = instances[i];
        switch (instance.GetPort()) {
          case kNormalPort:
            if (!instance.isHealthy()) {
              CHAOS_ERROR("%s instance %d heartbeat every 3 seconds is unhealthy", service_key_.name_.c_str(),
                          instance.GetPort());
            }
            break;
          case kNoHeartbeatPort:
            if (instance.isHealthy()) {
              CHAOS_ERROR("%s instance %d never heartbeat is healthy", service_key_.name_.c_str(), instance.GetPort());
            }
            break;
          case kSleepHeartbeatPort:
            if ((time(nullptr) / 60) % 2 && (time(nullptr) % 60 > 10) && !instance.isHealthy()) {
              CHAOS_ERROR("%s instance %d send heartbeat but unhealthy", service_key_.name_.c_str(),
                          instance.GetPort());
            }
            break;
          case kRandomHeartbeatPort:
            if (!instance.isHealthy()) {
              CHAOS_ERROR("%s instance %d heartbeat with random seconds is unhealthy", service_key_.name_.c_str(),
                          instance.GetPort());
            }
            break;
          default:
            break;
        }
      }
      delete resp;
      discover_time = time(nullptr) + 2;
    }

    // 禁止上报心跳错误
    if (disable_report_time < time(nullptr)) {
      InstanceHeartbeatRequest request(service_key_.namespace_, service_key_.name_, token_, kInstanceHost,
                                       kDisableHeartbeatPort);
      request.SetTimeout(2000);
      ret_code = provider_->Heartbeat(request);
      if (ret_code != kReturnHealthyCheckDisable) {
        CHAOS_ERROR("%s heartbeat for disable instance return :%s", service_key_.name_.c_str(),
                    ReturnCodeToMsg(ret_code).c_str());
      }
      disable_report_time = time(nullptr) + 60;
    }

    // token错误上报
    if (error_token_report_time < time(nullptr)) {
      InstanceHeartbeatRequest request(service_key_.namespace_, service_key_.name_, "token_abc", kInstanceHost,
                                       kErrorTokenPort);
      request.SetTimeout(2000);
      ret_code = provider_->Heartbeat(request);
      if (ret_code != kReturnUnauthorized) {
        CHAOS_ERROR("%s heartbeat for error token instance return :%s", service_key_.name_.c_str(),
                    ReturnCodeToMsg(ret_code).c_str());
      }
      error_token_report_time = time(nullptr) + 60 * 2;
    }

    // 实例不存在
    if (not_register_report_time < time(nullptr)) {
      InstanceHeartbeatRequest request(service_key_.namespace_, service_key_.name_, token_, kInstanceHost,
                                       kNotRegisterPort);
      request.SetTimeout(2000);
      ret_code = provider_->Heartbeat(request);
      if (ret_code != kReturnServiceNotFound) {
        CHAOS_ERROR("%s heartbeat for not exist instance return :%s", service_key_.name_.c_str(),
                    ReturnCodeToMsg(ret_code).c_str());
      }
      not_register_report_time = time(nullptr) + 60 * 3;
    }

    usleep(100 * 1000);
  }
  CHAOS_INFO("exit loop");
}

void HeartbeatChaos::TearDown() {
  if (provider_ != nullptr) {
    delete provider_;
    provider_ = nullptr;
  }
  if (consumer_ != nullptr) {
    delete consumer_;
    consumer_ = nullptr;
  }
}

}  // namespace polaris
