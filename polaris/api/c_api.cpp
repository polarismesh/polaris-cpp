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

#include "api/c_api.h"

#include <stdint.h>
#include <stdio.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "model/return_code.h"
#include "polaris/accessors.h"
#include "polaris/config.h"
#include "polaris/consumer.h"
#include "polaris/context.h"
#include "polaris/defs.h"
#include "polaris/log.h"
#include "polaris/model.h"
#include "polaris/polaris_api.h"
#include "polaris/provider.h"

namespace polaris {
extern const char* g_sdk_version_info;
}  // namespace polaris

#ifdef __cplusplus
extern "C" {
#endif
const char* polaris_get_version_info() { return polaris::g_sdk_version_info; }

void polaris_set_log_dir(const char* log_dir) { polaris::SetLogDir(log_dir); }

void polaris_set_log_level(PolarisLogLevel log_level) {
  polaris::GetLogger()->SetLogLevel(static_cast<polaris::LogLevel>(log_level));
}

const char* polaris_get_err_msg(int ret_code) {
  std::map<polaris::ReturnCode, polaris::ReturnCodeInfo>& return_code_map =
      polaris::ReturnCodeInfo::GetReturnCodeInfoMap();
  std::map<polaris::ReturnCode, polaris::ReturnCodeInfo>::const_iterator it =
      return_code_map.find(static_cast<polaris::ReturnCode>(ret_code));
  if (it != return_code_map.end()) {
    return it->second.message_;
  } else {
    return polaris::ReturnCodeInfo::GetUnkownErrorInfo().message_;
  }
}

///////////////////////////////////////////////////////////////////////////////

polaris_api* _polaris_api_new_from_config(polaris::Config* config) {
  polaris::Context* context = polaris::Context::Create(config, polaris::kShareContext);
  if (context == NULL) {
    delete config;
    return NULL;
  }
  _polaris_api* api  = new _polaris_api();
  api->context_      = context;
  api->consumer_api_ = polaris::ConsumerApi::Create(context);
  api->provider_api_ = polaris::ProviderApi::Create(context);
  delete config;
  return api;
}

polaris_api* polaris_api_new(void) {
  std::string err_msg;
  polaris::Config* config = polaris::Config::CreateWithDefaultFile(err_msg);
  if (config == NULL) {
    printf("create api with config error %s\n", err_msg.c_str());
  }
  return _polaris_api_new_from_config(config);
}

polaris_api* polaris_api_new_from(const char* config_file) {
  std::string err_msg;
  polaris::Config* config = polaris::Config::CreateFromFile(config_file, err_msg);
  if (config == NULL) {
    printf("create api with config error %s\n", err_msg.c_str());
    return NULL;
  }
  return _polaris_api_new_from_config(config);
}

polaris_api* polaris_api_new_from_content(const char* content) {
  std::string err_msg;
  polaris::Config* config = polaris::Config::CreateFromString(content, err_msg);
  if (config == NULL) {
    printf("create api from content with error: %s\n", err_msg.c_str());
    return NULL;
  }
  return _polaris_api_new_from_config(config);
}

void polaris_api_destroy(polaris_api** api) {
  delete (*api)->consumer_api_;
  delete (*api)->provider_api_;
  delete (*api)->context_;
  delete (*api);
  api = NULL;
}

///////////////////////////////////////////////////////////////////////////////

polaris_get_one_instance_req* polaris_get_one_instance_req_new(const char* service_namespace,
                                                               const char* service_name) {
  polaris::ServiceKey service_key;
  service_key.namespace_                = service_namespace;
  service_key.name_                     = service_name;
  polaris_get_one_instance_req* request = new polaris_get_one_instance_req();
  request->request_                     = new polaris::GetOneInstanceRequest(service_key);
  return request;
}

void polaris_get_one_instance_req_destroy(polaris_get_one_instance_req** get_one_instance_req) {
  delete (*get_one_instance_req)->request_;
  delete (*get_one_instance_req);
  get_one_instance_req = NULL;
}

void polaris_get_one_instance_req_set_src_service_key(
    polaris_get_one_instance_req* get_one_instance_req, const char* service_namespace,
    const char* service_name) {
  polaris::GetOneInstanceRequestAccessor accessor(*(get_one_instance_req->request_));
  if (!accessor.HasSourceService()) {
    polaris::ServiceInfo service_info;
    get_one_instance_req->request_->SetSourceService(service_info);
  }
  accessor.GetSourceService()->service_key_.namespace_ = service_namespace;
  accessor.GetSourceService()->service_key_.name_      = service_name;
}

void polaris_get_one_instance_req_add_src_service_metadata(
    polaris_get_one_instance_req* get_one_instance_req, const char* item_name,
    const char* item_value) {
  polaris::GetOneInstanceRequestAccessor accessor(*(get_one_instance_req->request_));
  if (!accessor.HasSourceService()) {
    polaris::ServiceInfo service_info;
    get_one_instance_req->request_->SetSourceService(service_info);
  }
  accessor.GetSourceService()->metadata_.insert(std::make_pair(item_name, item_value));
}

void polaris_get_one_instance_req_set_hash_key(polaris_get_one_instance_req* get_one_instance_req,
                                               uint64_t hash_key) {
  get_one_instance_req->request_->SetHashKey(hash_key);
}

void polaris_get_one_instance_req_set_hash_string(
    polaris_get_one_instance_req* get_one_instance_req, const char* hash_string) {
  get_one_instance_req->request_->SetHashString(hash_string);
}

void polaris_get_one_instance_req_set_ignore_half_open(
    polaris_get_one_instance_req* get_one_instance_req, bool ignore_half_open) {
  get_one_instance_req->request_->SetIgnoreHalfOpen(ignore_half_open);
}

void polaris_get_one_instance_req_set_src_set_name(
    polaris_get_one_instance_req* get_one_instance_req, const char* set_name) {
  get_one_instance_req->request_->SetSourceSetName(set_name);
}

void polaris_get_one_instance_req_set_canary(polaris_get_one_instance_req* get_one_instance_req,
                                             const char* canary) {
  get_one_instance_req->request_->SetCanary(canary);
}

void polaris_get_one_instance_req_set_timeout(polaris_get_one_instance_req* get_one_instance_req,
                                              uint64_t timeout) {
  get_one_instance_req->request_->SetTimeout(timeout);
}

void polaris_get_one_instance_req_metadata_add_item(
    polaris_get_one_instance_req* get_one_instance_req, const char* item_name,
    const char* item_value) {
  polaris::GetOneInstanceRequestAccessor accessor(*(get_one_instance_req->request_));
  if (accessor.GetMetadataParam() == NULL) {
    std::map<std::string, std::string> metadata;
    metadata.insert(std::make_pair(item_name, item_value));
    get_one_instance_req->request_->SetMetadata(metadata);
  }
  accessor.GetMetadataParam()->metadata_.insert(std::make_pair(item_name, item_value));
}

void polaris_get_one_instance_req_metadata_failover(
    polaris_get_one_instance_req* get_one_instance_req, PolarisMetadataFailoverType failover_type) {
  get_one_instance_req->request_->SetMetadataFailover(
      static_cast<polaris::MetadataFailoverType>(failover_type));
}

///////////////////////////////////////////////////////////////////////////////

polaris_get_instances_req* polaris_get_instances_req_new(const char* service_namespace,
                                                         const char* service_name) {
  polaris::ServiceKey service_key;
  service_key.namespace_             = service_namespace;
  service_key.name_                  = service_name;
  polaris_get_instances_req* request = new polaris_get_instances_req();
  request->request_                  = new polaris::GetInstancesRequest(service_key);
  return request;
}

void polaris_get_instances_req_destroy(polaris_get_instances_req** get_instances_req) {
  delete (*get_instances_req)->request_;
  delete (*get_instances_req);
  get_instances_req = NULL;
}

void polaris_get_instances_req_set_src_service_key(polaris_get_instances_req* get_instances_req,
                                                   const char* service_namespace,
                                                   const char* service_name) {
  polaris::ServiceInfo service_info;
  service_info.service_key_.namespace_ = service_namespace;
  service_info.service_key_.name_      = service_name;
  get_instances_req->request_->SetSourceService(service_info);
}

void polaris_get_instances_req_add_src_service_metadata(
    polaris_get_instances_req* get_instances_req, const char* item_name, const char* item_value) {
  polaris::GetInstancesRequestAccessor accessor(*(get_instances_req->request_));
  accessor.GetSourceService()->metadata_.insert(std::make_pair(item_name, item_value));
}

void polaris_get_instances_req_include_unhealthy(polaris_get_instances_req* get_instances_req,
                                                 bool include_unhealthy_instances) {
  get_instances_req->request_->SetIncludeUnhealthyInstances(include_unhealthy_instances);
}

void polaris_get_instances_req_include_circuit_break(polaris_get_instances_req* get_instances_req,
                                                     bool include_circuit_breaker_instances) {
  get_instances_req->request_->SetIncludeCircuitBreakInstances(include_circuit_breaker_instances);
}

void polaris_get_instances_req_skip_route_filter(polaris_get_instances_req* get_instances_req,
                                                 bool skip_route_filter) {
  get_instances_req->request_->SetSkipRouteFilter(skip_route_filter);
}

void polaris_get_instances_req_set_timeout(polaris_get_instances_req* get_instances_req,
                                           uint64_t timeout) {
  get_instances_req->request_->SetTimeout(timeout);
}

void polaris_get_instances_req_set_canary(polaris_get_instances_req* get_instances_req,
                                          const char* canary) {
  get_instances_req->request_->SetCanary(canary);
}

///////////////////////////////////////////////////////////////////////////////

void polaris_instance_destroy(polaris_instance** instance) {
  if (!(*instance)->is_ref_) {
    delete (*instance)->instance_;
  }
  delete (*instance);
  instance = NULL;
}

const char* polaris_instance_get_id(polaris_instance* instance) {
  return instance->instance_->GetId().c_str();
}

const char* polaris_instance_get_host(polaris_instance* instance) {
  return instance->instance_->GetHost().c_str();
}

int polaris_instance_get_port(polaris_instance* instance) { return instance->instance_->GetPort(); }

const char* polaris_instance_get_vpc_id(polaris_instance* instance) {
  return instance->instance_->GetVpcId().c_str();
}

uint32_t polaris_instance_get_weight(polaris_instance* instance) {
  return instance->instance_->GetWeight();
}

const char* polaris_instance_get_protocol(polaris_instance* instance) {
  return instance->instance_->GetProtocol().c_str();
}

const char* polaris_instance_get_version(polaris_instance* instance) {
  return instance->instance_->GetVersion().c_str();
}

int polaris_instance_get_priority(polaris_instance* instance) {
  return instance->instance_->GetPriority();
}

bool polaris_instance_is_healthy(polaris_instance* instance) {
  return instance->instance_->isHealthy();
}

const char* polaris_instance_get_metadata(polaris_instance* instance, const char* item_name) {
  std::map<std::string, std::string>& metadata             = instance->instance_->GetMetadata();
  std::map<std::string, std::string>::iterator metadata_it = metadata.find(item_name);
  if (metadata_it == metadata.end()) {
    return NULL;
  } else {
    return metadata_it->second.c_str();
  }
}

const char* polaris_instance_get_logic_set(polaris_instance* instance) {
  return instance->instance_->GetLogicSet().c_str();
}

const char* polaris_instance_get_region(polaris_instance* instance) {
  return instance->instance_->GetRegion().c_str();
}

const char* polaris_instance_get_zone(polaris_instance* instance) {
  return instance->instance_->GetZone().c_str();
}

const char* polaris_instance_get_campus(polaris_instance* instance) {
  return instance->instance_->GetCampus().c_str();
}

///////////////////////////////////////////////////////////////////////////////

void polaris_instances_resp_destroy(polaris_instances_resp** instances_resp) {
  delete (*instances_resp)->response_;
  delete (*instances_resp);
  instances_resp = NULL;
}

int polaris_instances_resp_size(polaris_instances_resp* instances_resp) {
  return instances_resp->response_->GetInstances().size();
}

polaris_instance* polaris_instances_resp_get_instance(polaris_instances_resp* instances_resp,
                                                      int index) {
  if (index < static_cast<int>(instances_resp->response_->GetInstances().size())) {
    polaris_instance* instance = new polaris_instance();
    instance->is_ref_          = true;
    instance->instance_        = &(instances_resp->response_->GetInstances()[index]);
    return instance;
  } else {
    return NULL;
  }
}

///////////////////////////////////////////////////////////////////////////////

int polaris_api_get_one_instance(polaris_api* api,
                                 polaris_get_one_instance_req* get_one_instance_req,
                                 polaris_instance** instance) {
  polaris_instance* get_instance = new polaris_instance();
  get_instance->is_ref_          = false;
  get_instance->instance_        = new polaris::Instance();
  polaris::ReturnCode ret_code   = api->consumer_api_->GetOneInstance(
      *(get_one_instance_req->request_), (*get_instance->instance_));
  if (ret_code == polaris::kReturnOk) {
    *instance = get_instance;
  } else {
    polaris_instance_destroy(&get_instance);
  }
  return static_cast<int>(ret_code);
}

int polaris_api_get_one_instance_resp(polaris_api* api,
                                      polaris_get_one_instance_req* get_one_instance_req,
                                      polaris_instances_resp** instances_resp) {
  polaris::InstancesResponse* get_response = NULL;
  polaris::ReturnCode ret_code =
      api->consumer_api_->GetOneInstance(*(get_one_instance_req->request_), get_response);
  if (ret_code == polaris::kReturnOk) {
    *instances_resp              = new polaris_instances_resp();
    (*instances_resp)->response_ = get_response;
  }
  return static_cast<int>(ret_code);
}

int polaris_api_get_instances_resp(polaris_api* api, polaris_get_instances_req* get_instances_req,
                                   polaris_instances_resp** instances_resp) {
  polaris::InstancesResponse* get_response = NULL;
  polaris::ReturnCode ret_code =
      api->consumer_api_->GetInstances(*(get_instances_req->request_), get_response);
  if (ret_code == polaris::kReturnOk) {
    *instances_resp              = new polaris_instances_resp();
    (*instances_resp)->response_ = get_response;
  }
  return static_cast<int>(ret_code);
}

int polaris_api_get_all_instances(polaris_api* api, polaris_get_instances_req* get_instances_req,
                                  polaris_instances_resp** instances_resp) {
  polaris::InstancesResponse* get_response = NULL;
  polaris::ReturnCode ret_code =
      api->consumer_api_->GetAllInstances(*(get_instances_req->request_), get_response);
  if (ret_code == polaris::kReturnOk) {
    *instances_resp              = new polaris_instances_resp();
    (*instances_resp)->response_ = get_response;
  }
  return static_cast<int>(ret_code);
}

///////////////////////////////////////////////////////////////////////////////

polaris_service_call_result* polaris_service_call_result_new(const char* service_namespace,
                                                             const char* service_name,
                                                             const char* instance_id) {
  polaris_service_call_result* call_result = new polaris_service_call_result();
  call_result->call_result_                = new polaris::ServiceCallResult();
  call_result->call_result_->SetServiceNamespace(service_namespace);
  call_result->call_result_->SetServiceName(service_name);
  call_result->call_result_->SetInstanceId(instance_id);
  return call_result;
}

void polaris_service_call_result_destroy(polaris_service_call_result** service_call_result) {
  delete (*service_call_result)->call_result_;
  delete (*service_call_result);
  service_call_result = NULL;
}

void polaris_service_call_result_set_ret_status(polaris_service_call_result* service_call_result,
                                                polaris_call_ret_status call_ret_status) {
  service_call_result->call_result_->SetRetStatus(
      static_cast<polaris::CallRetStatus>(call_ret_status));
}

void polaris_service_call_result_set_ret_code(polaris_service_call_result* service_call_result,
                                              int call_ret_code) {
  service_call_result->call_result_->SetRetCode(call_ret_code);
}

void polaris_service_call_result_set_delay(polaris_service_call_result* service_call_result,
                                           uint64_t delay) {
  service_call_result->call_result_->SetDelay(delay);
}

int polaris_api_update_service_call_result(polaris_api* api,
                                           polaris_service_call_result* service_call_result) {
  return api->consumer_api_->UpdateServiceCallResult(*service_call_result->call_result_);
}

///////////////////////////////////////////////////////////////////////////////

polaris_register_instance_req* polaris_register_instance_req_new(const char* service_namespace,
                                                                 const char* service_name,
                                                                 const char* service_token,
                                                                 const char* host, int port) {
  polaris_register_instance_req* request = new polaris_register_instance_req();
  request->request_ = new polaris::InstanceRegisterRequest(service_namespace, service_name,
                                                           service_token, host, port);
  return request;
}

void polaris_register_instance_req_destroy(polaris_register_instance_req** register_req) {
  delete (*register_req)->request_;
  delete (*register_req);
  register_req = NULL;
}

void polaris_register_instance_req_set_vpc_id(polaris_register_instance_req* register_req,
                                              const char* vpc_id) {
  register_req->request_->SetVpcId(vpc_id);
}

void polaris_register_instance_req_set_protocol(polaris_register_instance_req* register_req,
                                                const char* protocol) {
  register_req->request_->SetProtocol(protocol);
}

void polaris_register_instance_req_set_weight(polaris_register_instance_req* register_req,
                                              int weight) {
  register_req->request_->SetWeight(weight);
}

void polaris_register_instance_req_set_priority(polaris_register_instance_req* register_req,
                                                int priority) {
  register_req->request_->SetPriority(priority);
}

void polaris_register_instance_req_set_version(polaris_register_instance_req* register_req,
                                               const char* version) {
  register_req->request_->SetVersion(version);
}

void polaris_register_instance_req_add_metadata(polaris_register_instance_req* register_req,
                                                const char* key, const char* value) {
  polaris::InstanceRegisterRequestAccessor accessor(*register_req->request_);
  if (!accessor.HasMetadata()) {
    std::map<std::string, std::string> metadata;
    register_req->request_->SetMetadata(metadata);
  }
  accessor.GetMetadata().insert(std::make_pair(key, value));
}

void polaris_register_instance_req_set_health_check_flag(
    polaris_register_instance_req* register_req, bool health_check_flag) {
  register_req->request_->SetHealthCheckFlag(health_check_flag);
}

void polaris_register_instance_req_set_health_check_ttl(polaris_register_instance_req* register_req,
                                                        int ttl) {
  register_req->request_->SetTtl(ttl);
}

void polaris_register_instance_req_set_timeout(polaris_register_instance_req* register_req,
                                               uint64_t timeout) {
  register_req->request_->SetTimeout(timeout);
}

int polaris_api_register_instance(polaris_api* api, polaris_register_instance_req* req) {
  std::string instance_id;
  return api->provider_api_->Register(*req->request_, instance_id);
}

///////////////////////////////////////////////////////////////////////////////

polaris_deregister_instance_req* polaris_deregister_instance_req_new(const char* service_namespace,
                                                                     const char* service_name,
                                                                     const char* service_token,
                                                                     const char* host, int port) {
  polaris_deregister_instance_req* request = new polaris_deregister_instance_req();
  request->request_ = new polaris::InstanceDeregisterRequest(service_namespace, service_name,
                                                             service_token, host, port);
  return request;
}

void polaris_deregister_instance_req_destroy(polaris_deregister_instance_req** deregister_req) {
  delete (*deregister_req)->request_;
  delete (*deregister_req);
  deregister_req = NULL;
}

void polaris_deregister_instance_req_set_vpc_id(polaris_deregister_instance_req* deregister_req,
                                                const char* vpc_id) {
  deregister_req->request_->SetVpcId(vpc_id);
}

void polaris_deregister_instance_req_set_timeout(polaris_deregister_instance_req* deregister_req,
                                                 uint64_t timeout) {
  deregister_req->request_->SetTimeout(timeout);
}

int polaris_api_deregister_instance(polaris_api* api,
                                    polaris_deregister_instance_req* deregister_req) {
  return api->provider_api_->Deregister(*deregister_req->request_);
}

///////////////////////////////////////////////////////////////////////////////

polaris_instance_heartbeat_req* polaris_instance_heartbeat_req_new(const char* service_namespace,
                                                                   const char* service_name,
                                                                   const char* service_token,
                                                                   const char* host, int port) {
  polaris_instance_heartbeat_req* request = new polaris_instance_heartbeat_req();
  request->request_ = new polaris::InstanceHeartbeatRequest(service_namespace, service_name,
                                                            service_token, host, port);
  return request;
}

void polaris_instance_heartbeat_req_destroy(polaris_instance_heartbeat_req** heartbeat_req) {
  delete (*heartbeat_req)->request_;
  delete (*heartbeat_req);
  heartbeat_req = NULL;
}

void polaris_instance_heartbeat_req_set_vpc_id(polaris_instance_heartbeat_req* heartbeat_req,
                                               const char* vpc_id) {
  heartbeat_req->request_->SetVpcId(vpc_id);
}

void polaris_instance_heartbeat_req_set_timeout(polaris_instance_heartbeat_req* heartbeat_req,
                                                uint64_t timeout) {
  heartbeat_req->request_->SetTimeout(timeout);
}

int polaris_api_instance_heartbeat(polaris_api* api,
                                   polaris_instance_heartbeat_req* heartbeat_req) {
  return api->provider_api_->Heartbeat(*heartbeat_req->request_);
}

#ifdef __cplusplus
}
#endif
