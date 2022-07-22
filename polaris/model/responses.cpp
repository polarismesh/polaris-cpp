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

namespace polaris {

InstancesResponse::InstancesResponse() : impl_(new InstancesResponse::Impl()) {}

InstancesResponse::~InstancesResponse() {
  delete impl_;
  impl_ = nullptr;
}

uint64_t InstancesResponse::GetFlowId() { return impl_->flow_id_; }

std::string& InstancesResponse::GetServiceName() { return impl_->service_name_; }

std::string& InstancesResponse::GetServiceNamespace() { return impl_->service_namespace_; }

std::map<std::string, std::string>& InstancesResponse::GetMetadata() { return impl_->metadata_; }

WeightType InstancesResponse::GetWeightType() { return impl_->weight_type_; }

std::string& InstancesResponse::GetRevision() { return impl_->revision_; }

std::vector<Instance>& InstancesResponse::GetInstances() { return impl_->instances_; }

const std::map<std::string, std::string>& InstancesResponse::GetSubset() { return impl_->subset_; }

InstancesResponse::Impl& InstancesResponse::GetImpl() const { return *impl_; }

}  // namespace polaris
