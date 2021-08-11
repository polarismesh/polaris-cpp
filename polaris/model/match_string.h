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

#ifndef POLARIS_CPP_POLARIS_MODEL_MATCH_STRING_H_
#define POLARIS_CPP_POLARIS_MODEL_MATCH_STRING_H_

#include <re2/re2.h>

#include <map>
#include <string>

#include "context/system_variables.h"
#include "utils/shared_ptr.h"
#include "v1/model.pb.h"

namespace polaris {

class MatchString {
public:
  MatchString() : type_(v1::MatchString::EXACT), value_type_(v1::MatchString::TEXT) {}

  bool Init(const v1::MatchString& match_string);

  // 填充VARIABLE类型的值
  bool FillVariable(const std::string& variable);

  bool Match(const std::string& value) const;

  bool MatchParameter(const std::string& parameter, const std::string& value) const;

  bool IsExactText() const {
    return value_type_ == v1::MatchString::TEXT && type_ == v1::MatchString::EXACT;
  }

  bool IsRegex() const { return type_ == v1::MatchString::REGEX; }

  bool IsVariable() const { return value_type_ == v1::MatchString::VARIABLE; }

  bool IsParameter() const { return value_type_ == v1::MatchString::PARAMETER; }

  const std::string& GetString() const { return data_; }

  static const std::string& Wildcard();

  // 规则元数据匹配
  static bool MapMatch(const std::map<std::string, MatchString>& rule_metadata,
                       const std::map<std::string, std::string>& metadata);

  // 规则元数据匹配，返回paremeter类型的值
  static bool MapMatch(const std::map<std::string, MatchString>& rule_metadata,
                       const std::map<std::string, std::string>& metadata, std::string& parameters);

  // 规则元数据匹配，需要匹配Parameter类型值
  static bool MapMatch(const std::map<std::string, MatchString>& rule_metadata,
                       const std::map<std::string, std::string>& metadata,
                       const std::map<std::string, std::string>& parameters);

private:
  bool InitRegex(const std::string& regex);

private:
  v1::MatchString::MatchStringType type_;  // 匹配类型
  v1::MatchString::ValueType value_type_;  // 值类型
  std::string data_;
  SharedPtr<re2::RE2> regex_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_MATCH_STRING_H_
