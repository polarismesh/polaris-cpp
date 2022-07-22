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

#include "integration/common/integration_base.h"

#include <google/protobuf/util/json_util.h>
#include <fstream>
#include <streambuf>

#include "integration/common/environment.h"
#include "integration/common/http_client.h"
#include "v1/code.pb.h"

namespace polaris {

void IntegrationBase::SetUp() {
  if (service_.has_name()) {
    CreateService(service_, service_token_);  // 创建服务
  }
  if (context_ == nullptr && config_string_.empty()) {  // 以默认配置共享模式创建Context
    std::string err_msg;
    config_string_ =
        "global:\n"
        "  serverConnector:\n"
        "    addresses: [" +
        Environment::GetDiscoverServer() +
        "]\nconsumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        Environment::GetPersistDir() +
        "\n  circuitBreaker:\n"
        "    setCircuitBreaker:\n"
        "      enable: true\n";
    Config *config = Config::CreateFromString(config_string_, err_msg);
    ASSERT_TRUE(config != nullptr) << config_string_;
    context_ = Context::Create(config, kShareContext);
    ASSERT_TRUE(context_ != nullptr);
    delete config;
  }
}

void IntegrationBase::SetUpWithTrpc() {
  if (service_.has_name()) {
    CreateService(service_, service_token_);  // 创建服务
  }
  if (context_ == nullptr && config_string_.empty()) {  // 以默认配置共享模式创建Context
    std::string err_msg;
    config_string_ =
        "global:\n"
        "  serverConnector:\n"
        "    protocol: trpc\n"
        "    addresses: [" +
        Environment::GetTrpcDiscoverServer() +
        "]\nconsumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        Environment::GetPersistDir() +
        "\n  circuitBreaker:\n"
        "    setCircuitBreaker:\n"
        "      enable: true\n";
    Config *config = Config::CreateFromString(config_string_, err_msg);
    ASSERT_TRUE(config != nullptr) << config_string_;
    context_ = Context::Create(config, kShareContext);
    ASSERT_TRUE(context_ != nullptr);
    delete config;
  }
}

void IntegrationBase::TearDown() {
  if (!service_token_.empty()) {
    // 删除服务
    DeleteService(service_.name().value(), service_.namespace_().value(), service_token_);
  }
  if (context_ != nullptr) {
    delete context_;
    context_ = nullptr;
  }
}

void IntegrationBase::CreateService(v1::Service &service, std::string &out_tokens) {
  // 设置用户名
  service.mutable_owners()->set_value(Environment::GetPolarisUser());
  // 请求序列化成Json
  std::string request;
  google::protobuf::util::Status status = google::protobuf::util::MessageToJsonString(service, &request);
  ASSERT_EQ(status, status.OK) << service.ShortDebugString();
  request = "[" + request + "]";
  std::string response;
  int ret_code = HttpClient::DoRequest(HTTP_POST, "/naming/v1/services", request, 1000, response);
  ASSERT_EQ(ret_code, 200) << request << "\n" << response;
  v1::BatchWriteResponse bwr;
  status = google::protobuf::util::JsonStringToMessage(response, &bwr);
  ASSERT_EQ(status, status.OK) << response;
  ASSERT_EQ(bwr.code().value(), v1::ExecuteSuccess) << response;
  ASSERT_EQ(bwr.responses_size(), 1) << response;
  const v1::Response &service_response = bwr.responses(0);
  ASSERT_EQ(service_response.code().value(), v1::ExecuteSuccess) << response;
  out_tokens = service_response.service().token().value();
}

void IntegrationBase::DeleteService(const std::string &name, const std::string &space, const std::string &token) {
  v1::Service service;
  service.mutable_name()->set_value(name);
  service.mutable_namespace_()->set_value(space);
  service.mutable_token()->set_value(token);
  DeleteService(service);
}

void IntegrationBase::DeleteService(v1::Service &service) {
  // 请求序列化成Json
  std::string request;
  google::protobuf::util::Status status = google::protobuf::util::MessageToJsonString(service, &request);
  ASSERT_EQ(status, status.OK) << service.ShortDebugString();
  request = "[" + request + "]";
  std::string response;
  int ret_code = HttpClient::DoRequest(HTTP_POST, "/naming/v1/services/delete", request, 1000, response);
  ASSERT_EQ(ret_code, 200) << response;
}

void IntegrationBase::SendRequestAndAssertResponse(std::string &request, const std::string &path,
                                                   v1::BatchWriteResponse &bwr) {
  std::string response;
  request = "[" + request + "]";
  int ret_code = HttpClient::DoRequest(HTTP_POST, path, request, 1000, response);
  ASSERT_EQ(ret_code, 200) << request << "\n" << response;
  google::protobuf::util::Status status = google::protobuf::util::JsonStringToMessage(response, &bwr);
  ASSERT_EQ(status, status.OK) << response;
  ASSERT_EQ(bwr.code().value(), v1::ExecuteSuccess) << response;
  ASSERT_EQ(bwr.responses_size(), 1) << response;
  const v1::Response &instance_response = bwr.responses(0);
  ASSERT_EQ(instance_response.code().value(), v1::ExecuteSuccess) << response;
}

//添加实例
void IntegrationBase::AddPolarisServiceInstance(v1::Instance &instance, std::string &out_id) {
  // 请求序列化成Json
  std::string request;
  google::protobuf::util::Status status = google::protobuf::util::MessageToJsonString(instance, &request);
  ASSERT_EQ(status, status.OK) << instance.ShortDebugString();
  v1::BatchWriteResponse bwr;
  SendRequestAndAssertResponse(request, "/naming/v1/instances", bwr);
  out_id = bwr.responses(0).instance().id().value();
}

void IntegrationBase::AddPolarisServiceInstance(const std::string &service, const std::string &space,
                                                const std::string &token, const std::string &host, int port,
                                                std::map<std::string, std::string> &meta, bool isolate,
                                                std::string &out_id) {
  v1::Instance ins;
  ins.mutable_service_token()->set_value(token);
  ins.mutable_service()->set_value(service);
  ins.mutable_namespace_()->set_value(space);
  ins.mutable_host()->set_value(host);
  ins.mutable_port()->set_value(port);
  ins.mutable_isolate()->set_value(isolate);
  for (std::map<std::string, std::string>::iterator it = meta.begin(); it != meta.end(); ++it) {
    (*ins.mutable_metadata())[it->first] = it->second;
  }

  AddPolarisServiceInstance(ins, out_id);
}

void IntegrationBase::DeletePolarisServiceInstance(const std::string &token, const std::string &id) {
  v1::Instance instance;
  instance.mutable_id()->set_value(id);
  instance.mutable_service_token()->set_value(token);
  DeletePolarisServiceInstance(instance);
}

void IntegrationBase::DeletePolarisServiceInstance(v1::Instance &instance) {
  // 请求序列化成Json
  //删除是没有回复的
  std::string request;
  google::protobuf::util::Status status = google::protobuf::util::MessageToJsonString(instance, &request);
  ASSERT_EQ(status, status.OK) << instance.ShortDebugString();
  v1::BatchWriteResponse bwr;
  std::string response;
  request = "[" + request + "]";
  int ret_code = HttpClient::DoRequest(HTTP_POST, "/naming/v1/instances/delete", request, 1000, response);
  ASSERT_EQ(ret_code, 200) << request << "\n" << response;
}

//添加路由规则
void IntegrationBase::AddPolarisRouteRule(v1::Routing &route_rule) {
  std::string request;
  google::protobuf::util::Status status = google::protobuf::util::MessageToJsonString(route_rule, &request);
  ASSERT_EQ(status, status.OK) << route_rule.ShortDebugString();
  v1::BatchWriteResponse bwr;
  //路由规则多的时候，可能拿不到返回结果
  std::string response;
  request = "[" + request + "]";
  int ret_code = HttpClient::DoRequest(HTTP_POST, "/naming/v1/routings", request, 1000, response);
  ASSERT_EQ(ret_code, 200) << request << "\n" << response;
}

void IntegrationBase::DeletePolarisServiceRouteRule(const std::string &token, const std::string &name,
                                                    const std::string &space) {
  v1::Routing route_rule;
  route_rule.mutable_service()->set_value(name);
  route_rule.mutable_namespace_()->set_value(space);
  route_rule.mutable_service_token()->set_value(token);
  DeletePolarisServiceRouteRule(route_rule);
}
void IntegrationBase::DeletePolarisServiceRouteRule(v1::Routing &route_rule) {
  std::string request;
  google::protobuf::util::Status status = google::protobuf::util::MessageToJsonString(route_rule, &request);
  ASSERT_EQ(status, status.OK) << route_rule.ShortDebugString();
  v1::BatchWriteResponse bwr;
  std::string response;
  request = "[" + request + "]";
  int ret_code = HttpClient::DoRequest(HTTP_POST, "/naming/v1/routings/delete", request, 1000, response);
  ASSERT_EQ(ret_code, 200) << request << "\n" << response;
}

//更新路由
void IntegrationBase::UpdatePolarisRouteRule(v1::Routing &route_rule) {
  std::string request;
  google::protobuf::util::Status status = google::protobuf::util::MessageToJsonString(route_rule, &request);
  ASSERT_EQ(status, status.OK) << route_rule.ShortDebugString();
  v1::BatchWriteResponse bwr;
  std::string response;
  request = "[" + request + "]";
  int ret_code = HttpClient::DoRequest(HTTP_PUT, "/naming/v1/routings", request, 1000, response);
  ASSERT_EQ(ret_code, 200) << request << "\n" << response;
}

//添加熔断规则
void IntegrationBase::AddPolarisSetBreakerRule(v1::CircuitBreaker &circuit_breaker, const std::string &service_token,
                                               const std::string &version, std::string &out_token,
                                               std::string &out_id) {
  circuit_breaker.mutable_owners()->set_value(Environment::GetPolarisUser());  // 设置用户名
  //有3个步骤！
  std::string request;
  //创建
  google::protobuf::util::Status status = google::protobuf::util::MessageToJsonString(circuit_breaker, &request);
  ASSERT_EQ(status, status.OK) << circuit_breaker.ShortDebugString();
  v1::BatchWriteResponse bwr;
  SendRequestAndAssertResponse(request, "/naming/v1/circuitbreakers", bwr);
  //版本
  std::string token = bwr.responses(0).circuitbreaker().token().value();
  std::string id = bwr.responses(0).circuitbreaker().id().value();
  circuit_breaker.mutable_id()->set_value(id);
  circuit_breaker.mutable_token()->set_value(token);
  circuit_breaker.mutable_version()->set_value(version);
  out_token = token;
  out_id = id;
  request.clear();
  status = google::protobuf::util::MessageToJsonString(circuit_breaker, &request);
  ASSERT_EQ(status, status.OK) << circuit_breaker.ShortDebugString();
  SendRequestAndAssertResponse(request, "/naming/v1/circuitbreakers/version", bwr);
  //发布
  v1::ConfigRelease deploy;
  deploy.mutable_service()->mutable_name()->set_value(circuit_breaker.service().value());
  deploy.mutable_service()->mutable_namespace_()->set_value(circuit_breaker.service_namespace().value());
  deploy.mutable_service()->mutable_token()->set_value(service_token);
  deploy.mutable_circuitbreaker()->mutable_id()->set_value(id);
  deploy.mutable_circuitbreaker()->mutable_version()->set_value(version);
  deploy.mutable_circuitbreaker()->mutable_name()->set_value(circuit_breaker.name().value());
  deploy.mutable_circuitbreaker()->mutable_namespace_()->set_value(circuit_breaker.namespace_().value());
  request.clear();
  status = google::protobuf::util::MessageToJsonString(deploy, &request);
  ASSERT_EQ(status, status.OK) << deploy.ShortDebugString();
  std::string response;
  request = "[" + request + "]";
  int ret_code = HttpClient::DoRequest(HTTP_POST, "/naming/v1/circuitbreakers/release", request, 1000, response);
  ASSERT_EQ(ret_code, 200) << request << "\n" << response;
}

void IntegrationBase::DeletePolarisSetBreakerRule(const std::string &name, const std::string &version,
                                                  const std::string &token, const std::string &breaker_space,
                                                  const std::string &service_token, const std::string &service,
                                                  const std::string &space) {
  v1::CircuitBreaker circuit_breaker;
  circuit_breaker.mutable_name()->set_value(name);
  circuit_breaker.mutable_version()->set_value(version);
  circuit_breaker.mutable_namespace_()->set_value(breaker_space);
  circuit_breaker.mutable_service()->set_value(service);
  circuit_breaker.mutable_service_namespace()->set_value(space);
  circuit_breaker.mutable_token()->set_value(token);
  DeletePolarisSetBreakerRule(circuit_breaker, service_token);
}

void IntegrationBase::DeletePolarisSetBreakerRule(v1::CircuitBreaker &circuit_breaker,
                                                  const std::string &service_token) {
  //需要先解绑才能删除！
  v1::ConfigRelease deploy;
  deploy.mutable_service()->mutable_name()->set_value(circuit_breaker.service().value());
  deploy.mutable_service()->mutable_namespace_()->set_value(circuit_breaker.service_namespace().value());
  deploy.mutable_service()->mutable_token()->set_value(service_token);
  deploy.mutable_circuitbreaker()->mutable_id()->set_value(circuit_breaker.id().value());
  deploy.mutable_circuitbreaker()->mutable_version()->set_value(circuit_breaker.version().value());
  deploy.mutable_circuitbreaker()->mutable_name()->set_value(circuit_breaker.name().value());
  deploy.mutable_circuitbreaker()->mutable_namespace_()->set_value(circuit_breaker.namespace_().value());
  std::string request;
  google::protobuf::util::Status status = google::protobuf::util::MessageToJsonString(deploy, &request);
  ASSERT_EQ(status, status.OK) << deploy.ShortDebugString();
  std::string response;
  request = "[" + request + "]";
  int ret_code = HttpClient::DoRequest(HTTP_POST, "/naming/v1/circuitbreakers/unbind", request, 1000, response);
  ASSERT_EQ(ret_code, 200) << request << "\n" << response;
  //删除
  request.clear();
  response.clear();
  //删除master版本
  circuit_breaker.mutable_version()->set_value("master");
  status = google::protobuf::util::MessageToJsonString(circuit_breaker, &request);
  ASSERT_EQ(status, status.OK) << circuit_breaker.ShortDebugString();
  request = "[" + request + "]";
  ret_code = HttpClient::DoRequest(HTTP_POST, "/naming/v1/circuitbreakers/delete", request, 1000, response);
  ASSERT_EQ(ret_code, 200) << request << "\n" << response;
}

void IntegrationBase::CreateRateLimitRule(v1::Rule &rate_limit_rule, std::string &rule_id) {
  // 请求序列化成Json
  std::string request;
  google::protobuf::util::Status status = google::protobuf::util::MessageToJsonString(rate_limit_rule, &request);
  ASSERT_EQ(status, status.OK) << rate_limit_rule.ShortDebugString();
  request = "[" + request + "]";
  std::string response;
  int ret_code = HttpClient::DoRequest(HTTP_POST, "/naming/v1/ratelimits", request, 1000, response);
  ASSERT_EQ(ret_code, 200) << request << "\n" << response;
  v1::BatchWriteResponse bwr;
  status = google::protobuf::util::JsonStringToMessage(response, &bwr);
  ASSERT_EQ(status, status.OK) << response;
  ASSERT_EQ(bwr.code().value(), v1::ExecuteSuccess) << response;
  ASSERT_EQ(bwr.responses_size(), 1) << response;
  const v1::Response &first_resp = bwr.responses(0);
  ASSERT_EQ(first_resp.code().value(), v1::ExecuteSuccess) << response;
  ASSERT_TRUE(first_resp.has_ratelimit()) << response;
  rule_id = first_resp.ratelimit().id().value();
  ASSERT_TRUE(!rule_id.empty());
}

void IntegrationBase::DeleteRateLimitRule(const std::string &rule_id, const std::string &service_token) {
  v1::Rule rule;
  rule.mutable_id()->set_value(rule_id);
  rule.mutable_service_token()->set_value(service_token);
  DeleteRateLimitRule(rule);
}

void IntegrationBase::DeleteRateLimitRule(v1::Rule &rate_limit_rule) {
  // 请求序列化成Json
  std::string request;
  google::protobuf::util::Status status = google::protobuf::util::MessageToJsonString(rate_limit_rule, &request);
  ASSERT_EQ(status, status.OK) << rate_limit_rule.ShortDebugString();
  request = "[" + request + "]";
  std::string response;
  int ret_code = HttpClient::DoRequest(HTTP_POST, "/naming/v1/ratelimits/delete", request, 1000, response);
  ASSERT_EQ(ret_code, 200) << response;
}

void IntegrationBase::UpdateRateLimitRule(v1::Rule &rate_limit_rule) {
  // 请求序列化成Json
  std::string request;
  google::protobuf::util::Status status = google::protobuf::util::MessageToJsonString(rate_limit_rule, &request);
  ASSERT_EQ(status, status.OK) << rate_limit_rule.ShortDebugString();
  request = "[" + request + "]";
  std::string response;
  int ret_code = HttpClient::DoRequest(HTTP_PUT, "/naming/v1/ratelimits", request, 1000, response);
  ASSERT_EQ(ret_code, 200) << request << "\n" << response;
}

void IntegrationBase::ParseMessageFromJsonFile(const std::string &file, google::protobuf::Message *proto) {
  std::ifstream filestream(file.c_str());
  std::string jsonstr((std::istreambuf_iterator<char>(filestream)), std::istreambuf_iterator<char>());
  google::protobuf::util::Status status = google::protobuf::util::JsonStringToMessage(jsonstr, proto);
  filestream.close();
  ASSERT_EQ(status, status.OK) << file << jsonstr;
}

}  // namespace polaris
