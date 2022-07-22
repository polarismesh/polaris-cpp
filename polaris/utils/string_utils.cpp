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

#include "utils/string_utils.h"

#include <stdio.h>
#include <time.h>

namespace polaris {

std::string StringUtils::MapToStr(const std::map<std::string, std::string>& m) {
  std::string output;
  const char* separator = "";
  for (std::map<std::string, std::string>::const_iterator it = m.begin(); it != m.end(); ++it) {
    output += (separator + it->first + ":" + it->second);
    separator = "|";
  }
  return output;
}

std::string StringUtils::TimeToStr(uint64_t time_second) {
  struct tm tm;
  char time_buffer[64];
  time_t timer = static_cast<time_t>(time_second);
  if (!localtime_r(&timer, &tm)) {
    snprintf(time_buffer, sizeof(time_buffer), "error:localtime");
  } else if (0 == strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &tm)) {
    snprintf(time_buffer, sizeof(time_buffer), "error:strftime");
  }
  return time_buffer;
}

std::string StringUtils::StringTrim(const std::string& str) {
  std::size_t first = str.find_first_not_of(' ');
  if (std::string::npos == first) {
    return "";
  }
  std::size_t last = str.find_last_not_of(' ');
  return str.substr(first, (last - first + 1));
}

bool StringUtils::StringHasSuffix(const std::string& str, const std::string& suffix) {
  return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string StringUtils::JoinString(std::vector<std::string>& lists) {
  std::ostringstream output;
  for (std::size_t i = 0; i < lists.size(); ++i) {
    if (i > 0) {
      output << ", ";
    }
    output << lists[i];
  }
  return output.str();
}

bool StringUtils::IgnoreCaseCmp(const std::string& lhs, const std::string& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (tolower(lhs[i]) != tolower(rhs[i])) {
      return false;
    }
  }
  return true;
}

std::vector<std::string> StringUtils::SplitString(const std::string& in, const char& separator) {
  std::stringstream ss(in);
  std::string tmp_str;
  std::vector<std::string> vec;
  while (std::getline(ss, tmp_str, separator)) {
    if (!tmp_str.empty()) {
      vec.push_back(tmp_str);
    }
  }
  return vec;
}

}  // namespace polaris
