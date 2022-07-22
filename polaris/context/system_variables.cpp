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

#include "context/system_variables.h"

#include <stdlib.h>

namespace polaris {

void SystemVariables::InitFromConfig(const std::map<std::string, std::string>& variables) {
  if (!variables.empty()) {
    config_variables_.reset(new std::map<std::string, std::string>(variables));
  }
}

bool SystemVariables::GetVariable(const std::string& variable, std::string& value) const {
  if (config_variables_ != nullptr) {
    std::map<std::string, std::string>::const_iterator it = config_variables_->find(variable);
    if (it != config_variables_->end()) {
      value = it->second;
      return true;
    }
  }
  // 从系统环境变量中查询
  char* env_var = getenv(variable.c_str());
  if (env_var != nullptr) {
    value = env_var;
    return true;
  }
  return false;
}

}  // namespace polaris
