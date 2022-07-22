//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use
//  this file
//  except in compliance with the License. You may obtain a copy of the License
//  at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the
//  specific
//  language governing permissions and limitations under the License.

#ifndef POLARIS_CPP_TEST_MOCK_FAKE_SERVER_RESPONSE_H_
#define POLARIS_CPP_TEST_MOCK_FAKE_SERVER_RESPONSE_H_

#include <string>

#include "polaris/defs.h"
#include "polaris/plugin.h"
#include "utils/utils.h"
#include "v1/code.pb.h"
#include "v1/response.pb.h"

namespace polaris {

class FakeServer {
 public:
  static void SetService(v1::DiscoverResponse &response, const ServiceKey &service_key,
                         const std::string version = "init_version") {
    v1::Service *service = response.mutable_service();
    service->mutable_namespace_()->set_value(service_key.namespace_);
    service->mutable_name()->set_value(service_key.name_);
    service->mutable_revision()->set_value(version);
  }

  static void InstancesResponse(v1::DiscoverResponse &response, const ServiceKey &service_key,
                                const std::string version = "init_version") {
    response.set_type(v1::DiscoverResponse::INSTANCE);
    SetService(response, service_key, version);
  }

  static void RoutingResponse(v1::DiscoverResponse &response, const ServiceKey &service_key,
                              const std::string version = "init_version") {
    response.set_type(v1::DiscoverResponse::ROUTING);
    SetService(response, service_key, version);
  }

  static void CreateServiceInstances(v1::DiscoverResponse &response, const ServiceKey &service_key, int instance_num,
                                     int index_begin = 0) {
    response.Clear();
    response.mutable_code()->set_value(v1::ExecuteSuccess);
    FakeServer::InstancesResponse(response, service_key, "version_one");
    for (int i = 0; i < instance_num; i++) {
      ::v1::Instance *instance = response.add_instances();
      instance->mutable_namespace_()->set_value(service_key.namespace_);
      instance->mutable_service()->set_value(service_key.name_);
      instance->mutable_id()->set_value("instance_" + std::to_string(index_begin + i));
      instance->mutable_host()->set_value("host_" + std::to_string(index_begin + i));
      instance->mutable_port()->set_value(1000 + i);
      instance->mutable_weight()->set_value(100);
      instance->mutable_location()->mutable_region()->set_value("华南");
      instance->mutable_location()->mutable_zone()->set_value("深圳");
      instance->mutable_location()->mutable_campus()->set_value("深圳-大学城");
    }
  }

  static void CreateServiceRoute(v1::DiscoverResponse &response, const ServiceKey &service_key, bool need_router) {
    response.Clear();
    response.mutable_code()->set_value(v1::ExecuteSuccess);
    FakeServer::RoutingResponse(response, service_key, "version_one");
    v1::MatchString exact_string;
    if (need_router) {
      ::v1::Routing *routing = response.mutable_routing();
      routing->mutable_namespace_()->set_value(service_key.namespace_);
      routing->mutable_service()->set_value(service_key.name_);
      ::v1::Route *route = routing->add_inbounds();
      v1::Source *source = route->add_sources();
      source->mutable_namespace_()->set_value(service_key.namespace_);
      source->mutable_service()->set_value(service_key.name_);
      exact_string.mutable_value()->set_value("base");
      (*source->mutable_metadata())["env"] = exact_string;
      for (int i = 0; i < 2; ++i) {
        v1::Destination *destination = route->add_destinations();
        destination->mutable_namespace_()->set_value("*");
        destination->mutable_service()->set_value("*");
        exact_string.mutable_value()->set_value(i == 0 ? "base" : "test");
        (*destination->mutable_metadata())["env"] = exact_string;
        destination->mutable_priority()->set_value(i);
      }
    }
  }

  static ReturnCode InitService(LocalRegistry *local_registry, const ServiceKey &service_key, int instance_num,
                                bool need_router) {
    ReturnCode ret_code;
    ServiceDataNotify *data_notify = nullptr;
    ServiceData *service_data = nullptr;
    ret_code = local_registry->LoadServiceDataWithNotify(service_key, kServiceDataInstances, service_data, data_notify);
    if (ret_code != kReturnOk) {
      return ret_code;
    }
    ret_code = local_registry->LoadServiceDataWithNotify(service_key, kServiceDataRouteRule, service_data, data_notify);
    if (ret_code != kReturnOk) {
      return ret_code;
    }
    v1::DiscoverResponse response;
    CreateServiceInstances(response, service_key, instance_num);
    service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
    ret_code = local_registry->UpdateServiceData(service_key, kServiceDataInstances, service_data);
    if (ret_code != kReturnOk) {
      return ret_code;
    }
    CreateServiceRoute(response, service_key, need_router);
    service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
    ret_code = local_registry->UpdateServiceData(service_key, kServiceDataRouteRule, service_data);
    if (ret_code != kReturnOk) {
      return ret_code;
    }
    return kReturnOk;
  }

  static void CreateServiceRateLimit(v1::DiscoverResponse &response, const ServiceKey &service_key, int qps) {
    response.Clear();
    response.mutable_code()->set_value(v1::ExecuteSuccess);
    response.set_type(v1::DiscoverResponse::RATE_LIMIT);
    SetService(response, service_key, "version_one");
    v1::RateLimit *rate_limit = response.mutable_ratelimit();
    rate_limit->mutable_revision()->set_value("version_one");
    v1::Rule *rule = rate_limit->add_rules();
    rule->mutable_id()->set_value("4b42d711e0e0414e8bc2567b9140ba09");
    rule->mutable_namespace_()->set_value(service_key.namespace_);
    rule->mutable_service()->set_value(service_key.name_);
    rule->set_type(v1::Rule::LOCAL);
    v1::MatchString match_string;
    match_string.set_type(v1::MatchString::REGEX);
    match_string.mutable_value()->set_value("v*");
    (*rule->mutable_subset())["subset"] = match_string;
    (*rule->mutable_labels())["label"] = match_string;
    v1::Amount *amount = rule->add_amounts();
    amount->mutable_maxamount()->set_value(qps);
    amount->mutable_validduration()->set_seconds(1);
    rule->mutable_revision()->set_value("5483700359f342bcba4421cc58e8a9cd");
  }
};

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_MOCK_FAKE_SERVER_RESPONSE_H_
