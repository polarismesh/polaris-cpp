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

#include "model/route_rule_source.h"

namespace polaris {

bool RouteRuleSource::InitFromPb(const v1::Source& source) {
  service_key_.namespace_ = source.namespace_().value();
  service_key_.name_      = source.service().value();
  google::protobuf::Map<std::string, v1::MatchString>::const_iterator it;
  for (it = source.metadata().begin(); it != source.metadata().end(); ++it) {
    if (!metadata_[it->first].Init(it->second)) {
      return false;
    }
  }
  return true;
}

bool RouteRuleSource::FillSystemVariables(const SystemVariables& variables) {
  for (std::map<std::string, MatchString>::iterator it = metadata_.begin(); it != metadata_.end();
       ++it) {
    if (it->second.IsVariable()) {
      const std::string& variable_value = it->second.GetString();
      std::string value;
      if (!variable_value.empty() && variables.GetVariable(variable_value, value)) {
        if (!it->second.FillVariable(value)) {
          return false;
        }
      }
    }
  }
  return true;
}

bool RouteRuleSource::Match(const ServiceKey& service_key,
                            const std::map<std::string, std::string>& metadata,
                            std::string& parameter) const {
  return (service_key_.namespace_ == service_key.namespace_ ||
          service_key_.namespace_ == MatchString::Wildcard()) &&
         (service_key_.name_ == service_key.name_ ||
          service_key_.name_ == MatchString::Wildcard()) &&
         MatchString::MapMatch(metadata_, metadata, parameter);
}

bool RouteRuleSource::IsWildcardRule() const {
  return service_key_.namespace_ == MatchString::Wildcard() &&
         service_key_.name_ == MatchString::Wildcard() && metadata_.empty();
}

}  // namespace polaris
