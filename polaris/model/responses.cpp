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

#include "model/responses.h"

#include <stddef.h>

#include "polaris/accessors.h"
#include "polaris/consumer.h"

namespace polaris {

InstancesResponse::InstancesResponse() { impl = new InstancesResponseImpl(); }

InstancesResponse::~InstancesResponse() {
  if (impl != NULL) delete impl;
}

uint64_t InstancesResponse::GetFlowId() { return impl->flow_id_; }

std::string& InstancesResponse::GetServiceName() { return impl->service_name_; }

std::string& InstancesResponse::GetServiceNamespace() { return impl->service_namespace_; }

std::map<std::string, std::string>& InstancesResponse::GetMetadata() { return impl->metadata_; }

WeightType InstancesResponse::GetWeightType() { return impl->weight_type_; }

std::string& InstancesResponse::GetRevision() { return impl->revision_; }

std::vector<Instance>& InstancesResponse::GetInstances() { return impl->instances_; }

const std::map<std::string, std::string>& InstancesResponse::GetSubset() { return impl->subset_; }

void InstancesResponseSetter::SetFlowId(const uint64_t flow_id) {
  response_.impl->flow_id_ = flow_id;
}

void InstancesResponseSetter::SetServiceName(const std::string& service_name) {
  response_.impl->service_name_ = service_name;
}

void InstancesResponseSetter::SetServiceNamespace(const std::string& service_namespace) {
  response_.impl->service_namespace_ = service_namespace;
}

void InstancesResponseSetter::SetMetadata(const std::map<std::string, std::string>& metadata) {
  response_.impl->metadata_ = metadata;
}

void InstancesResponseSetter::SetWeightType(WeightType weight_type) {
  response_.impl->weight_type_ = weight_type;
}

void InstancesResponseSetter::SetRevision(const std::string& revision) {
  response_.impl->revision_ = revision;
}

void InstancesResponseSetter::AddInstance(const Instance& instance) {
  response_.impl->instances_.push_back(instance);
}

void InstancesResponseSetter::SetSubset(const std::map<std::string, std::string>& subset) {
  response_.impl->subset_ = subset;
}

}  // namespace polaris
