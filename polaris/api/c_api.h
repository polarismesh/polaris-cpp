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

#ifndef POLARIS_CPP_POLARIS_API_C_API_H_
#define POLARIS_CPP_POLARIS_API_C_API_H_

namespace polaris {
class ConsumerApi;
class Context;
class GetInstancesRequest;
class GetOneInstanceRequest;
class Instance;
class InstanceDeregisterRequest;
class InstanceHeartbeatRequest;
class InstanceRegisterRequest;
class InstancesResponse;
class ProviderApi;
class ServiceCallResult;
}  // namespace polaris

#ifdef __cplusplus
extern "C" {
#endif

struct _polaris_api {
  polaris::Context* context_;
  polaris::ConsumerApi* consumer_api_;
  polaris::ProviderApi* provider_api_;
};

struct _polaris_get_one_instance_req {
  polaris::GetOneInstanceRequest* request_;
};

struct _polaris_get_instances_req {
  polaris::GetInstancesRequest* request_;
};

struct _polaris_instance {
  bool is_ref_;
  polaris::Instance* instance_;
};

struct _polaris_instances_resp {
  polaris::InstancesResponse* response_;
};

struct _polaris_service_call_result {
  polaris::ServiceCallResult* call_result_;
};

struct _polaris_register_instance_req {
  polaris::InstanceRegisterRequest* request_;
};

struct _polaris_deregister_instance_req {
  polaris::InstanceDeregisterRequest* request_;
};

struct _polaris_instance_heartbeat_req {
  polaris::InstanceHeartbeatRequest* request_;
};

#ifdef __cplusplus
}
#endif

#endif  //  POLARIS_CPP_POLARIS_API_C_API_H_