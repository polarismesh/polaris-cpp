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

#ifndef POLARIS_CPP_POLARIS_UTILS_UTILS_H_
#define POLARIS_CPP_POLARIS_UTILS_UTILS_H_

#include <stdint.h>

#include <string>

namespace polaris {

#ifdef __GNUC__
#  define POLARIS_LIKELY(x) __builtin_expect((x), 1)
#  define POLARIS_UNLIKELY(x) __builtin_expect((x), 0)
#else /* __GNUC__ */
#  define POLARIS_LIKELY(x) (x)
#  define POLARIS_UNLIKELY(x) (x)
#endif /* __GNUC__ */

///////////////////////////////////////////////////////////////////////////////
class Utils {
 public:
  static uint64_t GetNextSeqId();

  static uint32_t GetNextSeqId32();

  static std::string UrlEncode(const std::string& url);

  static std::string UrlDecode(const std::string& url);

  static int HexcharToInt(char input);

  static bool HexStringToBytes(const std::string& hex_string, std::string* out_bytes);

  static std::string Uuid();

  static bool IsPrime(uint64_t n);
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_UTILS_UTILS_H_
