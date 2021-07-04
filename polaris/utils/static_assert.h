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

#ifndef POLARIS_CPP_POLARIS_UTILS_STATIC_ASSERT_H_
#define POLARIS_CPP_POLARIS_UTILS_STATIC_ASSERT_H_

// A portable static assert.
#if __cplusplus >= 201103L
#define STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define STATIC_ASSERT(condition, message) STATIC_ASSERT_LINE(condition, __LINE__)
#define STATIC_ASSERT_LINE(condition, line) \
  typedef char STATIC_ASSERT_failed_at_##line[(2 * static_cast<int>(!!(condition))) - 1]
#endif

#endif  //  POLARIS_CPP_POLARIS_UTILS_STATIC_ASSERT_H_
