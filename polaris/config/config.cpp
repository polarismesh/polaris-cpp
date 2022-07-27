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

/// @brief 将文件内容读入字符串中
std::string readFileIntoString(const std::string& path) {
    std::ifstream input_file(path);
    return std::string((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());
}

/// @brief 从文件创建配置对象
Config* Config::CreateFromFile(const std::string& config_file, std::string& err_msg) {
  if (!FileUtils::FileExists(config_file)) {
    err_msg = "create config with file " + config_file + " not exists";
    return nullptr;
  }
  std::string content = readFileIntoString(config_file);
  return CreateFromString(content, err_msg);
}

/// @brief 支持环境变量展开
static std::string expand_environment_variables( const std::string &s ) {
    if( s.find( "${" ) == std::string::npos ) return s;

    std::string pre  = s.substr( 0, s.find( "${" ) );
    std::string post = s.substr( s.find( "${" ) + 2 );

    if( post.find( '}' ) == std::string::npos ) return s;

    std::string variable = post.substr( 0, post.find( '}' ) );
    std::string value    = "";

    post = post.substr( post.find( '}' ) + 1 );

    const char *v = getenv( variable.c_str() );
    if( v != NULL ) value = std::string( v );

    return expand_environment_variables( pre + value + post );
}

const std::string emptyConfigContent = R"##(
global:
  serverConnector:
    addresses:
    - 127.0.0.1:8091
)##";

/// @brief 从字符串创建配置对象
Config* Config::CreateFromString(const std::string& content, std::string& err_msg) {
  std::string expandedContent = expand_environment_variables(content);
  if (expandedContent.empty()) {
    expandedContent = emptyConfigContent;
  }
  ConfigImpl* impl = new ConfigImpl();
  try {
    std::istringstream strstream(expandedContent);
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
Config* Config::CreateEmptyConfig() {
  std::string err_msg;
  return CreateFromString(emptyConfigContent, err_msg);
}

}  // namespace polaris
