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

#ifndef POLARIS_CPP_POLARIS_UTILS_STRING_UTILS_H_
#define POLARIS_CPP_POLARIS_UTILS_STRING_UTILS_H_

#include <ctype.h>
#include <stdint.h>

#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace polaris {

class StringUtils {
public:
  /// @brief 字符串转换成整型类型
  template <typename T>
  static bool SafeStrToType(const std::string& str, T& value);

  /// @brief 类型转换成字符串
  template <typename T>
  static std::string TypeToStr(T value);

  static std::string MapToStr(const std::map<std::string, std::string>& m);

  static std::string TimeToStr(uint64_t time_second);

  static std::string StringTrim(const std::string& str);

  static bool StringHasSuffix(const std::string& str, const std::string& suffix);

  static std::string JoinString(std::vector<std::string>& lists);

  // 忽略大小写比较字符串是否相等
  static bool IgnoreCaseCmp(const std::string& lhs, const std::string& rhs);
};

template <typename T>
bool StringUtils::SafeStrToType(const std::string& str, T& value) {
  T result = 0;
  for (std::size_t i = 0; i < str.size(); ++i) {
    if (isdigit(str[i])) {
      result = result * 10 + (str[i] - '0');
    } else {
      return false;
    }
  }
  value = result;
  return true;
}

template <typename T>
std::string StringUtils::TypeToStr(T value) {
  std::stringstream ss;
  ss << value;
  return ss.str();
}

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_UTILS_STRING_UTILS_H_
