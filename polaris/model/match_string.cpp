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

#include "model/match_string.h"

#include "logger.h"

namespace polaris {

bool MatchString::Init(const v1::MatchString& match_string) {
  type_ = match_string.type();
  value_type_ = match_string.value_type();
  data_ = match_string.value().value();
  regex_.reset();
  allMatch = data_.empty() || data_ == ALL_MATCH;
  if (!allMatch) {
    if (type_ == v1::MatchString::REGEX) {
      return InitRegex(match_string.value().value());
    }
  }
  return true;
}

bool MatchString::InitRegex(const std::string& regex) {
  regex_.reset(new re2::RE2(regex, RE2::Quiet));
  if (regex_->ok()) {
    return true;
  } else {  // 编译出错输出日志
    POLARIS_LOG(LOG_ERROR, "regex string [%s] compile error: %s", regex_->pattern().c_str(), regex_->error().c_str());
    return false;
  }
}

bool MatchString::FillVariable(const std::string& variable) {
  POLARIS_ASSERT(value_type_ == v1::MatchString::VARIABLE);
  data_ = variable;
  if (type_ == v1::MatchString::REGEX) {
    return InitRegex(data_);
  }
  return true;
}

bool MatchString::Match(const std::string& value) const {
  if (value_type_ == v1::MatchString::PARAMETER) {
    return true;  // 参数类型不在这里匹配value
  }
  if (allMatch) {
    return true;
  }
  if (type_ == v1::MatchString::EXACT) {
    return data_ == value;
  }
  if (regex_ != nullptr) {
    return re2::RE2::PartialMatch(value.c_str(), *regex_);
  }
  return false;
}

bool MatchString::MatchParameter(const std::string& parameter, const std::string& value) const {
  if (type_ == v1::MatchString::EXACT) {
    return parameter == value;
  } else if (type_ == v1::MatchString::REGEX) {
    re2::RE2 regex(parameter, RE2::Quiet);
    return regex.ok() && re2::RE2::PartialMatch(value.c_str(), regex);
  }
  return false;
}

const std::string& MatchString::WildcardOrValue(const std::string& value) {
  static std::string empty_string;
  return value != "*" ? value : empty_string;
}

bool MatchString::MapMatch(const std::map<std::string, MatchString>& rule_metadata,
                           const std::map<std::string, std::string>& metadata) {
  if (rule_metadata.size() > metadata.size()) {
    return false;
  }
  std::map<std::string, std::string>::const_iterator meta_it;
  for (std::map<std::string, MatchString>::const_iterator rule_it = rule_metadata.begin();
       rule_it != rule_metadata.end(); ++rule_it) {
    if ((meta_it = metadata.find(rule_it->first)) == metadata.end() || !rule_it->second.Match(meta_it->second)) {
      return false;
    }
  }
  return true;
}

bool MatchString::MapMatch(const std::map<std::string, MatchString>& rule_metadata,
                           const std::map<std::string, std::string>& metadata, std::string& parameters) {
  if (rule_metadata.size() > metadata.size()) {
    return false;
  }
  const char* separator = "";
  parameters.clear();
  std::map<std::string, std::string>::const_iterator meta_it;
  for (std::map<std::string, MatchString>::const_iterator rule_it = rule_metadata.begin();
       rule_it != rule_metadata.end(); ++rule_it) {
    if ((meta_it = metadata.find(rule_it->first)) == metadata.end()) {
      return false;
    }
    if (rule_it->second.IsParameter()) {
      parameters = separator + meta_it->second;
      separator = ",";
    } else {
      if (!rule_it->second.Match(meta_it->second)) {
        return false;
      }
    }
  }
  return true;
}

bool MatchString::MapMatch(const std::map<std::string, MatchString>& rule_metadata,
                           const std::map<std::string, std::string>& metadata,
                           const std::map<std::string, std::string>& parameters) {
  if (rule_metadata.size() > metadata.size()) {
    return false;
  }
  std::map<std::string, std::string>::const_iterator meta_it;
  for (std::map<std::string, MatchString>::const_iterator rule_it = rule_metadata.begin();
       rule_it != rule_metadata.end(); ++rule_it) {
    if ((meta_it = metadata.find(rule_it->first)) == metadata.end()) {
      return false;
    }
    if (rule_it->second.IsParameter()) {
      std::map<std::string, std::string>::const_iterator param_it = parameters.find(rule_it->first);
      if (param_it == parameters.end() || !rule_it->second.MatchParameter(param_it->second, meta_it->second)) {
        return false;
      }
    } else {
      if (!rule_it->second.Match(meta_it->second)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace polaris
