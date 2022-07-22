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

#ifndef POLARIS_CPP_TEST_INTEGRATION_COMMON_INTEGRATION_BASE_H_
#define POLARIS_CPP_TEST_INTEGRATION_COMMON_INTEGRATION_BASE_H_

#include <gtest/gtest.h>

#include <string>

#include "polaris/context.h"
#include "v1/response.pb.h"
#include "v1/service.pb.h"

namespace polaris {

class IntegrationBase : public ::testing::Test {
 protected:
  IntegrationBase() : context_(nullptr) {}

  virtual void SetUp();

  // 走trpc协议
  virtual void SetUpWithTrpc();

  virtual void TearDown();

 public:
  // 创建服务
  static void CreateService(v1::Service &service, std::string &out_tokens);
  // 删除服务
  static void DeleteService(const std::string &name, const std::string &space, const std::string &token);
  static void DeleteService(v1::Service &service);

  static void SendRequestAndAssertResponse(std::string &request, const std::string &path, v1::BatchWriteResponse &bwr);

  //添加实例
  static void AddPolarisServiceInstance(const std::string &service, const std::string &space, const std::string &token,
                                        const std::string &host, int port, std::map<std::string, std::string> &meta,
                                        bool isolate, std::string &out_id);

  static void AddPolarisServiceInstance(v1::Instance &instance, std::string &out_id);
  //删除实例
  static void DeletePolarisServiceInstance(const std::string &token, const std::string &id);
  static void DeletePolarisServiceInstance(v1::Instance &instance);
  //添加路由规则
  static void AddPolarisRouteRule(v1::Routing &route_rule);
  //删除路由规则
  static void DeletePolarisServiceRouteRule(const std::string &token, const std::string &name,
                                            const std::string &space);
  static void DeletePolarisServiceRouteRule(v1::Routing &route_rule);

  static void UpdatePolarisRouteRule(v1::Routing &route_rule);

  //添加熔断规则
  static void AddPolarisSetBreakerRule(v1::CircuitBreaker &circuit_breaker, const std::string &service_token,
                                       const std::string &version, std::string &out_token, std::string &out_id);
  //删除熔断规则
  static void DeletePolarisSetBreakerRule(const std::string &name, const std::string &version, const std::string &token,
                                          const std::string &breaker_space, const std::string &service_token,
                                          const std::string &service, const std::string &space);

  static void DeletePolarisSetBreakerRule(v1::CircuitBreaker &circuit_breaker, const std::string &service_token);

  // 创建服务限流规则
  static void CreateRateLimitRule(v1::Rule &rate_limit_rule, std::string &rule_id);
  // 删除服务限流规则
  static void DeleteRateLimitRule(const std::string &rule_id, const std::string &service_token);
  static void DeleteRateLimitRule(v1::Rule &rate_limit_rule);
  // 更新服务限流规则
  static void UpdateRateLimitRule(v1::Rule &rate_limit_rule);

  static void ParseMessageFromJsonFile(const std::string &file, google::protobuf::Message *proto);

 protected:
  v1::Service service_;
  std::string service_token_;
  Context *context_;
  std::string config_string_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_INTEGRATION_COMMON_INTEGRATION_BASE_H_