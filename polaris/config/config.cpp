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

#include "polaris/config.h"

#include <stddef.h>
#include <yaml-cpp/yaml.h>
#include <fstream>

#include "config_impl.h"
#include "utils/file_utils.h"

namespace polaris {

Config::Config(ConfigImpl* impl) { impl_ = impl; }

Config::~Config() {
  if (impl_ != nullptr) {
    delete impl_;
    impl_ = nullptr;
  }
}

/// @brief 从文件创建配置对象
Config* Config::CreateFromFile(const std::string& config_file, std::string& err_msg) {
  if (!FileUtils::FileExists(config_file)) {
    err_msg = "create config with file " + config_file + " not exists";
    return nullptr;
  }
  ConfigImpl* impl = new ConfigImpl();
  try {
    std::ifstream fin(config_file.c_str());
    YAML_0_3::Parser parser(fin);
    parser.GetNextDocument(*impl->data_);
  } catch (const YAML_0_3::Exception& e) {
    err_msg = "create config with config file[ " + config_file + "] error:" + e.what();
    delete impl;
    return nullptr;
  }
  return new Config(impl);
}

/// @brief 从字符串创建配置对象
Config* Config::CreateFromString(const std::string& content, std::string& err_msg) {
  ConfigImpl* impl = new ConfigImpl();
  try {
    std::istringstream strstream(content);
    YAML_0_3::Parser parser(strstream);
    parser.GetNextDocument(*impl->data_);
  } catch (const YAML_0_3::Exception& e) {
    err_msg = "create config with content[" + content + "] error:" + e.what();
    delete impl;
    return nullptr;
  }
  return new Config(impl);
}

Config* Config::CreateWithDefaultFile(std::string& err_msg) {
  std::string file_name = "./polaris.yaml";
  if (!FileUtils::FileExists(file_name)) {
    return Config::CreateEmptyConfig();
  } else {
    return Config::CreateFromFile(file_name, err_msg);
  }
}

/// @brief 创建空的配置，会使用默认配置
Config* Config::CreateEmptyConfig() { return new Config(new ConfigImpl()); }

}  // namespace polaris
