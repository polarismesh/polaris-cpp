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

// 将依赖的高版本libc函数指向低版本函数

#ifdef COMPILE_FOR_PRE_CPP11

#  include <string.h>

// memcpy
extern "C" {
void* __wrap_memcpy(void* dest, const void* src, size_t n) { return memcpy(dest, src, n); }
}
asm(".symver memcpy, memcpy@GLIBC_2.2.5");

// clock_gettime
__asm__(".symver clock_gettime,clock_gettime@GLIBC_2.2.5");

#endif  // COMPILE_FOR_PRE_CPP11
