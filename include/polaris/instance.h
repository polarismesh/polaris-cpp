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

#ifndef POLARIS_CPP_INCLUDE_POLARIS_INSTANCE_H_
#define POLARIS_CPP_INCLUDE_POLARIS_INSTANCE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace polaris {

class InstanceImpl;

/// @brief 服务实例
///
/// 主要包括三部分数据：远程下发数据、SDK本地数据、服务发现请求数据
class Instance {
 public:
  Instance();

  explicit Instance(std::shared_ptr<InstanceImpl> impl);

  Instance(const std::string& id, const std::string& host, const int& port, const uint32_t& weight);

  Instance(const Instance& other);

  const Instance& operator=(const Instance& other);

  ~Instance();

  const std::string& GetId() const;  ///< 服务实例ID

  const std::string& GetHost() const;  ///< 服务的节点IP或者域名

  int GetPort() const;  ///< 节点端口号

  bool IsIpv6() const;  /// 是否为ipv6

  uint64_t GetLocalId() const;  /// 本地生成的唯一ID

  const std::string& GetVpcId() const;  ///< 获取服务实例所在VIP ID

  uint32_t GetWeight() const;  ///< 实例静态权重值, 0-1000

  const std::string& GetProtocol() const;  ///< 实例协议信息

  const std::string& GetVersion() const;  ///< 实例版本号信息

  int GetPriority() const;  ///< 实例优先级

  bool IsHealthy() const;  ///< 实例健康状态

  bool IsIsolate() const;  ///< 实例隔离状态

  // [[deprecated("使用IsHealthy()")]]
  bool isHealthy() const;  ///<  实例健康状态

  // [[deprecated("使用IsIsolate()")]]
  bool isIsolate() const;  ///<  实例隔离状态

  const std::map<std::string, std::string>& GetMetadata() const;  ///< 实例元数据信息

  const std::string& GetContainerName() const;  ///< 实例元数据信息中的容器名

  const std::string& GetInternalSetName() const;  ///< 实例元数据信息中的set名

  const std::string& GetLogicSet() const;  ///< 实例LogicSet信息

  uint32_t GetDynamicWeight() const;  ///< 实例动态权重

  const std::string& GetRegion() const;  ///< location region

  const std::string& GetZone() const;  ///< location zone

  const std::string& GetCampus() const;  ///< location campus

  uint64_t GetHash() const;

  uint64_t GetLocalityAwareInfo() const;  // locality_aware_info

  InstanceImpl& GetImpl();

 private:
  std::shared_ptr<InstanceImpl> impl_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_INSTANCE_H_
