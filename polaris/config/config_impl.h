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

#ifndef POLARIS_CPP_POLARIS_CONFIG_CONFIG_IMPL_H_
#define POLARIS_CPP_POLARIS_CONFIG_CONFIG_IMPL_H_

#include <string>

namespace YAML_0_3 {
class Emitter;
class Node;
}  // namespace YAML_0_3

namespace polaris {

// 配置接口实现
class ConfigImpl {
public:
  ConfigImpl();

  // 用于构造根配置
  explicit ConfigImpl(YAML_0_3::Node* data);

  // 用于构造子配置
  ConfigImpl(YAML_0_3::Node* data, YAML_0_3::Emitter* emitter, YAML_0_3::Emitter* json_emitter);

  ~ConfigImpl();

private:
  friend class Config;
  template <typename T>
  friend T GetOrDefault(ConfigImpl* impl_, const std::string& key, const T& default_value);

  bool is_sub_config_;
  YAML_0_3::Node* data_;
  YAML_0_3::Emitter* emitter_;
  YAML_0_3::Emitter* json_emitter_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CONFIG_CONFIG_IMPL_H_
