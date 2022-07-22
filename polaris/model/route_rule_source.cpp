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
  src_service_.namespace_ = MatchString::WildcardOrValue(source.namespace_().value());
  src_service_.name_ = MatchString::WildcardOrValue(source.service().value());
  dst_service_.namespace_ = MatchString::WildcardOrValue(source.to_namespace().value());
  dst_service_.name_ = MatchString::WildcardOrValue(source.to_service().value());
  google::protobuf::Map<std::string, v1::MatchString>::const_iterator it;
  for (it = source.metadata().begin(); it != source.metadata().end(); ++it) {
    if (!metadata_[it->first].Init(it->second)) {
      return false;
    }
  }
  return true;
}

bool RouteRuleSource::FillSystemVariables(const SystemVariables& variables) {
  for (std::map<std::string, MatchString>::iterator it = metadata_.begin(); it != metadata_.end(); ++it) {
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

static bool MatchService(const ServiceKey& rule_service, const ServiceKey& input_service) {
  return (rule_service.namespace_.empty() || rule_service.namespace_ == input_service.namespace_) &&
         (rule_service.name_.empty() || rule_service.name_ == input_service.name_);
}

bool RouteRuleSource::Match(const ServiceKey& src_service, const ServiceKey& dst_service,
                            const std::map<std::string, std::string>& metadata, std::string& parameter) const {
  return MatchService(src_service_, src_service) && MatchService(dst_service_, dst_service) &&
         MatchString::MapMatch(metadata_, metadata, parameter);
}

bool RouteRuleSource::IsWildcardRule() const {
  return src_service_.namespace_.empty() && src_service_.name_.empty() && metadata_.empty();
}

}  // namespace polaris
