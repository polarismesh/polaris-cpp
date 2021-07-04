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

#include "utils/utils.h"

#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#include <sstream>
#include <string>

namespace polaris {

static const uint64_t kSeqIdBase = 1000000000000000;
uint64_t g_seq_id                = 0;
uint64_t Utils::GetNextSeqId() { return kSeqIdBase + ATOMIC_INC(&g_seq_id) % kSeqIdBase; }

void hexchar(unsigned char c, unsigned char& hex1, unsigned char& hex2) {
  hex1 = c / 16;
  hex2 = c % 16;
  hex1 += hex1 <= 9 ? '0' : 'a' - 10;
  hex2 += hex2 <= 9 ? '0' : 'a' - 10;
}

std::string Utils::UrlEncode(const std::string& url) {
  static const std::string UrlChars = "-_.~*'()";
  std::string result;
  for (std::size_t i = 0; i < url.size(); i++) {
    char c = url[i];
    if (isalnum(c) || UrlChars.find(c) != std::string::npos) {
      result.push_back(c);
    } else if (c == ' ') {
      result.push_back('+');
    } else if (isascii(c)) {
      result.push_back('%');
      unsigned char d1 = c / 16, d2 = c % 16;
      result.push_back(d1 + (d1 <= 9 ? '0' : 'a' - 10));
      result.push_back(d2 + (d2 <= 9 ? '0' : 'a' - 10));
    } else {
      result.push_back(c);
    }
  }
  return result;
}

std::string Utils::UrlDecode(const std::string& url) {
  std::string result;
  for (std::size_t i = 0; i < url.size(); i++) {
    if (url[i] == '%' && i + 2 < url.size() && isxdigit(url[i + 1]) && isxdigit(url[i + 2])) {
      char c1 = url[i + 1], c2 = url[i + 2];
      unsigned char ch = (isdigit(c1) ? c1 - '0' : tolower(c1) - 'a' + 10) << 4 |
                         (isdigit(c2) ? c2 - '0' : tolower(c2) - 'a' + 10);
      result.push_back(ch);
      i += 2;
    } else if (url[i] == '+') {
      result.push_back(' ');
    } else {
      result.push_back(url[i]);
    }
  }
  return result;
}

int Utils::HexcharToInt(char input) {
  if (input >= '0' && input <= '9') {
    return input - '0';
  }
  if (input >= 'A' && input <= 'F') {
    return input - 'A' + 10;
  }
  if (input >= 'a' && input <= 'f') {
    return input - 'a' + 10;
  }
  return -1;
}

bool Utils::HexStringToBytes(const std::string& hex_string, std::string* out_bytes) {
  if (NULL == out_bytes) {
    return false;
  }
  std::string hex_input_string = hex_string;
  if (hex_input_string.size() <= 2 ||
      hex_input_string.size() % 2) {  // 必须以0x开头，必须是偶数个字符
    return false;
  }
  if (hex_input_string.substr(0, 2) != "0x" &&
      hex_input_string.substr(0, 2) != "0X") {  // 必须以0x开头
    return false;
  }
  hex_input_string = hex_input_string.substr(2);
  for (std::size_t i = 0; i < hex_input_string.size(); ++i) {
    if (!std::isxdigit(hex_input_string[i])) {
      return false;  // 非法的16进制字符串
    }
  }

  int buffer_size  = hex_input_string.size() / 2 + 1;
  char* dst_buffer = static_cast<char*>(malloc(buffer_size));
  if (NULL == dst_buffer) {
    return false;
  }
  int dst_buffer_len = 0;
  for (size_t i = 1; i < hex_input_string.size(); i += 2) {
    dst_buffer[dst_buffer_len++] =
        HexcharToInt(hex_input_string[i - 1]) * 16 + HexcharToInt(hex_input_string[i]);
  }
  out_bytes->assign(dst_buffer, dst_buffer_len);
  free(dst_buffer);
  dst_buffer = NULL;
  return true;
}

const char XCHARS[] = "0123456789ABCDEF";
std::string Utils::Uuid() {
  std::string uuid         = std::string(36, '-');
  uuid[14]                 = '4';
  static unsigned int seed = time(NULL) ^ pthread_self();
  ATOMIC_ADD(&seed, seed ^ pthread_self());
  srand(seed);
  for (int i = 0; i < 36; i++) {
    if (i != 8 && i != 13 && i != 14 && i != 18 && i != 23) {
      uuid[i] = XCHARS[(i == 19) ? (rand() & 0x3) | 0x8 : rand() & 0xf];
    }
  }
  return uuid;
}

bool Utils::IsPrime(uint64_t n) {
  if (n <= 3) {
    return n > 1;
  }
  uint64_t sqrtN = static_cast<uint64_t>(sqrt(static_cast<double>(n)));
  for (uint64_t i = 2; i <= sqrtN; ++i) {
    if (0 == n % i) {
      return false;
    }
  }
  return true;
}

}  // namespace polaris
