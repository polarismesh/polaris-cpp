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

#ifndef POLARIS_CPP_POLARIS_MODEL_RESPONSES_H_
#define POLARIS_CPP_POLARIS_MODEL_RESPONSES_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "polaris/consumer.h"

namespace polaris {

class InstancesResponse::Impl {
 public:
  uint64_t flow_id_;
  std::string service_name_;
  std::string service_namespace_;
  std::map<std::string, std::string> metadata_;
  WeightType weight_type_;
  std::string revision_;
  std::vector<Instance> instances_;

  std::map<std::string, std::string> subset_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_RESPONSES_H_
