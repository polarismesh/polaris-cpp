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

/// \mainpage Polaris C++ API
/// The Polaris C++ API mainly consists of the following classes:
/// - Provider API: api for register/deregister/heartbeat service.
/// - Consumer API: api for discover services.
/// - Plugin Interfaces: interface for user define custom plugin.
/// - Context: context contain plugins init from config.

#ifndef POLARIS_CPP_INCLUDE_POLARIS_POLARIS_H_
#define POLARIS_CPP_INCLUDE_POLARIS_POLARIS_H_

#include <string>

#include "polaris/accessors.h"
#include "polaris/config.h"
#include "polaris/consumer.h"
#include "polaris/context.h"
#include "polaris/defs.h"
#include "polaris/log.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "polaris/provider.h"

namespace polaris {

/// @brief 获取字符串形式的版本号
std::string GetVersion();

/// @brief 获取字符串形式的版本信息
std::string GetVersionInfo();

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_POLARIS_H_
