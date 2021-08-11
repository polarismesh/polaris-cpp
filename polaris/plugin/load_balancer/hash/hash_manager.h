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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_HASH_HASH_MANAGER_H_
#define POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_HASH_HASH_MANAGER_H_

#include <stdint.h>

#include <map>
#include <string>

#include "polaris/defs.h"
#include "sync/mutex.h"

namespace polaris {

// 暂时只加 64 位的, 后面再加 32/128 位的吧
typedef uint64_t (*Hash64Func)(const void* key, const int32_t len, const uint32_t seed);

/// @desc 哈希函数管理器
class HashManager {
public:
  HashManager();

  ~HashManager();

  ReturnCode RegisterHashFunction(const std::string& name, Hash64Func func);

  ReturnCode GetHashFunction(const std::string& name, Hash64Func& func);

  static HashManager& Instance();

private:
  sync::Mutex lock_;
  std::map<std::string, Hash64Func> mapHash64Func_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_HASH_HASH_MANAGER_H_
