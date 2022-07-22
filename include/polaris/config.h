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

/// @file config.h
/// @brief 定义配置相关接口
///
#ifndef POLARIS_CPP_INCLUDE_POLARIS_CONFIG_H_
#define POLARIS_CPP_INCLUDE_POLARIS_CONFIG_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "polaris/noncopyable.h"

namespace polaris {

class ConfigImpl;
/// @brief 配置接口
///
/// 配置可用于初始化上下文和插件
class Config : Noncopyable {
 public:
  ~Config();

  /// @brief 获取子配置
  ///
  /// @note 函数返回值永不为NULL。当key不存在时，函数返回内容为空的Config。用户需要负责进行释放该指针
  /// @param key 要获取的子配置对象对应的key。
  /// @return Config* 返回的配置对象
  Config* GetSubConfig(const std::string& key);

  /// @brief 检查子配置是否存在
  ///
  /// @param key 要检查的子配置对象的key
  /// @return true 子配置对象存在
  /// @return false 子配置对象不存在
  bool SubConfigExist(const std::string& key);

  /// @brief 获取子配置列表
  ///
  /// @note 函数返回值永不为NULL。当key不存在时，函数返回内容为空的列表。用户需要负责进行释放该列表指针
  /// @param key 要获取的子配置对象对应的key。
  /// @return std::vector<Config*> 返回的配置对象列表
  std::vector<Config*> GetSubConfigList(const std::string& key);

  /// @brief 拷贝一份子配置，与父配置完全独立
  ///
  /// @note
  /// 函数返回值永不为NULL。当key不存在时，函数返回内容为空的Config。用户需要负责进行释放该指针
  /// @param key 要获取的子配置对象对应的key。
  /// @return Config* 返回的配置对象
  Config* GetSubConfigClone(const std::string& key);

  /// @brief 根据key获取字符串类型配置，Key不存在则创建并设置成默认值后返回默认值
  ///
  /// @param key 需要获取的配置key。
  /// @param default_value 默认值，当不存在或解析失败时返回默认值
  /// @return std::string 返回的值
  std::string GetStringOrDefault(const std::string& key, const std::string& default_value);

  /// @brief 根据Key获取整数类型配置，Key不存在则创建并设置成默认值后返回默认值
  ///
  /// @param key 需要获取的配置key。
  /// @param default_value 默认值，当不存在或解析失败时返回默认值
  /// @return int 返回的值
  int GetIntOrDefault(const std::string& key, int default_value);

  /// @brief 根据Key获取布尔类型配置，Key不存在则创建并设置成默认值后返回默认值
  ///
  /// @param key 需要获取的配置key。
  /// @param default_value 默认值，当不存在或解析失败时返回默认值
  /// @return bool 返回的值
  bool GetBoolOrDefault(const std::string& key, bool default_value);

  /// @brief 根据Key获取浮点类型配置，Key不存在则创建并设置成默认值后返回默认值
  ///
  /// @param key 需要获取的配置key。
  /// @param default_value 默认值，当不存在或解析失败时返回默认值
  /// @return float 返回的配置
  float GetFloatOrDefault(const std::string& key, float default_value);

  /// @brief 根据Key获取时间类型配置，Key不存在则创建并设置成默认值后返回默认值
  ///
  /// 时间类型配置支持四种格式，小时：1h； 分钟 10m； 秒：30s，毫秒：500ms
  /// @param key 需要获取的配置key。
  /// @param default_value 默认值，当key不存在或解析失败时返回
  /// @return uint64_t 返回的配置
  uint64_t GetMsOrDefault(const std::string& key, uint64_t default_value);

  /// @brief 根据Key获取列表类型配置，Key不存在则创建并设置成默认值后返回默认值
  ///
  /// @param key 需要获取的配置key。
  /// @param default_value
  /// 字符串形式默认值，当取不到key时，或者key不为序列时使用该字符串解析出序列，用逗号分隔
  /// @return std::vector<std::string> 返回的配置
  std::vector<std::string> GetListOrDefault(const std::string& key, const std::string& default_value);

  /// @brief 根据Key获取map类型配置，key不存在时返回空map
  ///
  /// @param key 需要获取的配置key
  /// @return std::map<std::string, std::string> 返回的配置
  std::map<std::string, std::string> GetMap(const std::string& key);

  /// @brief 获取配置最顶层的key名字
  ///
  /// @return const std::string& 顶层key
  const std::string& GetRootKey() const;

  /// @brief 配置转换成字符串
  ///
  /// @return std::string 返回的字符串
  std::string ToString();

  /// @brief 配置转换成Json格式的字符串
  ///
  /// @return std::string Json格式字符串
  std::string ToJsonString();

  /// @brief 从YAML配置文件创建配置对象
  ///
  /// @param config_file YAML格式配置文件路径
  /// @param err_msg 初始化失败时通过该参数回传错误信息
  /// @return Config* 创建的配置对象，创建失败时返回NULL
  static Config* CreateFromFile(const std::string& config_file, std::string& err_msg);

  /// @brief 从字符串形式的YAML配置中创建Config对象
  ///
  /// @param content YAML/JSON格式的字符串 如：{root1: {key1: value1, key2: [seq1, seq2]}, root2:
  /// value2}
  /// @param err_msg 初始化失败时通过该参数回传错误信息
  /// @return Config* 创建的配置对象，创建失败时返回NULL
  static Config* CreateFromString(const std::string& content, std::string& err_msg);

  /// @brief 从默认文件创建配置对象，默认文件为./polaris.yaml，文件不存在则使用默认配置
  ///
  /// @param err_msg 初始化失败时通过该参数回传错误信息
  /// @return Config* 从默认文件创建的对象
  static Config* CreateWithDefaultFile(std::string& err_msg);

  /// @brief 创建一个空的配置对象
  ///
  /// @return Config* 空的配置对象
  static Config* CreateEmptyConfig();

 private:
  explicit Config(ConfigImpl* impl);  // 隐藏构造函数，只能通过Create系列方法创建配置对象
  ConfigImpl* impl_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_CONFIG_H_
