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

#include "polaris/polaris.h"

#include <string>

namespace polaris {

#ifndef VERSION
#  define VERSION "0.13.4"
#endif  // !VERSION

#ifndef REVISION
#  define REVISION "NO_REVISION"
#endif  // !REVISION

const char* g_sdk_type = "polaris-cpp";
const char* g_sdk_version = VERSION;
const char* g_sdk_version_info = "polaris version:" VERSION "_" REVISION "_" __DATE__ " " __TIME__;

std::string GetVersion() { return g_sdk_version; }

std::string GetVersionInfo() { return g_sdk_version_info; }

}  // namespace polaris
