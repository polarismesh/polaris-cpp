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

#ifndef POLARIS_CPP_POLARIS_NETWORK_CALLBACKS_H_
#define POLARIS_CPP_POLARIS_NETWORK_CALLBACKS_H_

#include <functional>

#include "polaris/defs.h"

namespace polaris {

/// @brief 连接建立回调
///
/// ReturnCode分别为以下三种状态：
///   - kReturnOk：连接建立成功
///   - kReturnTimeout：连接建立超时
///   - kReturnNetworkFailed：网络错误
using ConnectionCallback = std::function<void(ReturnCode return_code)>;

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_NETWORK_CALLBACKS_H_
