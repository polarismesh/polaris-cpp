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

#include "hash_manager.h"

#include <utility>
#include "murmur.h"
#include "utils/indestructible.h"

namespace polaris {

HashManager& HashManager::Instance() {
  static Indestructible<HashManager> instance;
  return *instance.Get();
}

HashManager::HashManager() { RegisterHashFunction("murmur3", Murmur3_64); }

HashManager::~HashManager() {}

ReturnCode HashManager::RegisterHashFunction(const std::string& name, Hash64Func func) {
  ReturnCode code = kReturnOk;
  sync::MutexGuard mutex_guard(lock_);
  std::map<std::string, Hash64Func>::iterator it = mapHash64Func_.find(name);
  if (it == mapHash64Func_.end()) {
    mapHash64Func_[name] = func;
  } else {
    code = kReturnExistedResource;
  }
  return code;
}

ReturnCode HashManager::GetHashFunction(const std::string& name, Hash64Func& func) {
  ReturnCode code = kReturnOk;
  sync::MutexGuard mutex_guard(lock_);
  std::map<std::string, Hash64Func>::iterator it = mapHash64Func_.find(name);
  if (it != mapHash64Func_.end()) {
    func = it->second;
  } else {
    code = kReturnResourceNotFound;
  }
  return code;
}

}  // namespace polaris