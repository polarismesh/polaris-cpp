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

#include "plugin/load_balancer/hash/murmur.h"

#include "MurmurHash3.h"

namespace polaris {

uint64_t Murmur3_64(const void *key, const int32_t len, const uint32_t seed) {
  uint64_t hash[2];
  ::MurmurHash3_x64_128(key, len, seed, static_cast<void *>(hash));
  return hash[0];
}

uint32_t Murmur3_32(const void *key, const int32_t len, const uint32_t seed) {
  uint32_t hash[1];
  ::MurmurHash3_x86_32(key, len, seed, static_cast<void *>(hash));
  return hash[0];
}

}  // namespace polaris
