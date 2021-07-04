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

#ifndef POLARIS_CPP_POLARIS_CONFIG_SEED_SERVER_H_
#define POLARIS_CPP_POLARIS_CONFIG_SEED_SERVER_H_

#include <stdint.h>

#include <iosfwd>
#include <string>
#include <vector>

#include "polaris/defs.h"

namespace polaris {

// 种子服务器，可以通过该服务器发现更多的服务器
struct SeedServer {
  std::string ip_;
  int port_;
};

// 埋点Polaris服务配置，用于对Polaris服务器集群进行服务发现
struct PolarisCluster {
  explicit PolarisCluster(std::string service);

  void Update(const std::string service_namespace, const std::string& service_name);

  ServiceKey service_;
  uint64_t refresh_interval_;
};

class SeedServerConfig {
public:
  SeedServerConfig();

  /// @brief 设置接入点
  ///
  /// @param join_point 接入点名字
  /// @return ReturnCode 返回码
  ReturnCode UpdateJoinPoint(std::string join_point);

  /// @brief 获取种子服务器
  ///
  /// @param seed_servers 种子服务器
  /// @return int 种子服务器数量
  std::size_t GetSeedServer(std::vector<SeedServer>& seed_servers);

  /// @brief 获取默认种子服务器
  ///
  /// @param seed_servers 默认种子服务器
  /// @return int 默认种子服务器数量
  static std::size_t GetDefaultSeedServer(std::vector<SeedServer>& seed_servers);

  /// @brief 从字符串格式的配置中解析配置的种子服务器
  ///
  /// @param config_servers 字符串格式种子服务器
  /// @param seed_servers 解析到的种子服务器
  /// @return std::size_t 解析到的种子服务器数量
  static std::size_t ParseSeedServer(const std::vector<std::string>& config_servers,
                                     std::vector<SeedServer>& seed_servers);

  /// @brief 格式化种子服务器用于输出日志
  static std::string SeedServersToString(std::vector<SeedServer>& seed_servers);

public:
  std::string seed_server_;           // 埋点IP地址
  PolarisCluster discover_cluster_;   // 内置polaris server服务
  PolarisCluster heartbeat_cluster_;  // 内置heartbeat服务
  PolarisCluster monitor_cluster_;    // 内置monitor服务
  PolarisCluster metric_cluster_;     // 内置metric服务
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CONFIG_SEED_SERVER_H_
