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

#include <string>

#include "polaris/defs.h"
#include "sync/cond_var.h"

struct timespec;

namespace polaris {
namespace model {

struct VersionedLocation {
  Location location_;
  int version_;

  std::string LocationToString();
  std::string ToString();
};

/// @brief  客户端位置信息，用于进行就近路由
///
/// 用户可以通过配置文件配置
/// 如果用户不配置，则使用客户端IP向服务器查询
class ClientLocation {
public:
  ClientLocation();

  ~ClientLocation();

  // 通过配置加载，需检查location是否有效
  void Init(const Location& location);

  // 是否包含有效的位置信息
  bool WaitInit(uint64_t timeout);
  bool WaitInit(timespec& ts);

  // 从服务器查询到位置信息后更新，需检查location是否有效
  void Update(const Location& location);

  int GetVersion() { return version_; }

  void GetVersionedLocation(VersionedLocation& versioned_location);

private:
  sync::CondVarNotify notify_;
  volatile int version_;
  Location location_;
};

}  // namespace model
}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_LOCATION_H_
