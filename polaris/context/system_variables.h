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

#ifndef POLARIS_CPP_POLARIS_CONTEXT_SYSTEM_VARIABLES_H_
#define POLARIS_CPP_POLARIS_CONTEXT_SYSTEM_VARIABLES_H_

#include <map>
#include <string>

#include "polaris/noncopyable.h"
#include "utils/scoped_ptr.h"

namespace polaris {

class SystemVariables : Noncopyable {
public:
  // 使用配置中传入的环境变量初始化
  void InitFromConfig(const std::map<std::string, std::string>& variables);

  // 通过环境变量名获取对应的值
  // 配置中不存在时，会降级从系统环境变量里读取
  bool GetVariable(const std::string& variable, std::string& value) const;

private:
  // 通过配置传入的环境变量
  ScopedPtr<std::map<std::string, std::string> > config_variables_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CONTEXT_SYSTEM_VARIABLES_H_
