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

#ifndef POLARIS_CPP_POLARIS_UTILS_FORK_H_
#define POLARIS_CPP_POLARIS_UTILS_FORK_H_

namespace polaris {

extern int polaris_fork_count;

#define POLARIS_FORK_CHECK()                                      \
  if (context_impl->GetCreateForkCount() != polaris_fork_count) { \
    return kRetrunCallAfterFork;                                  \
  }

/// @brief 设置fork回调函数
void SetupCallbackAtfork();

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_UTILS_FORK_H_
