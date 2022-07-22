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

#ifndef POLARIS_CPP_POLARIS_UTILS_OPTIONAL_H_
#define POLARIS_CPP_POLARIS_UTILS_OPTIONAL_H_

namespace polaris {

/// @brief 可选字段类型
template <class T>
class optional {
 public:
  optional() : has_value_(false) {}

  bool HasValue() const { return has_value_; }

  const T& Value() const { return value_; }

  T& Value() { return value_; }

  T& operator=(T const& value) {
    has_value_ = true;
    value_ = value;
    return value_;
  }

  void SetHasValue() { has_value_ = true; }

 private:
  bool has_value_;
  T value_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_UTILS_OPTIONAL_H_
