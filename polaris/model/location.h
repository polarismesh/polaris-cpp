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

#ifndef POLARIS_CPP_POLARIS_MODEL_LOCATION_H_
#define POLARIS_CPP_POLARIS_MODEL_LOCATION_H_

#include <stdint.h>

#include <atomic>
#include <string>

#include "sync/cond_var.h"

namespace polaris {

/// @brief 三级位置信息
struct Location {
  std::string region;
  std::string zone;
  std::string campus;

  std::string ToString() const;

  // location有字段不为空，表示location有效
  bool IsValid() const;
};

/// @brief  客户端位置信息，用于进行就近路由
///
/// 用户可以通过配置文件配置
/// 如果用户不配置，则使用客户端IP向服务器查询
class ClientLocation {
 public:
  ClientLocation() : version_(0), enable_update_(true) {}

  ~ClientLocation() = default;

  // 通过配置加载，需检查location是否有效
  void Init(const Location& location, bool enable_update);

  // 是否包含有效的位置信息
  bool WaitInit(uint64_t timeout);

  // 从服务器查询到位置信息后更新，需检查location是否有效
  void Update(const Location& location);

  // 获取位置信息版本号
  uint32_t GetVersion() const { return version_.load(std::memory_order_relaxed); }

  // 获取位置信息
  void GetLocation(Location& location);

  // 获取位置信息和版本号
  void GetLocation(Location& location, uint32_t& version);

  // 转换成字符串
  static std::string ToString(const Location& location, uint32_t version);

 private:
  sync::CondVarNotify notify_;
  std::atomic<uint32_t> version_;
  bool enable_update_;
  Location location_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_LOCATION_H_
