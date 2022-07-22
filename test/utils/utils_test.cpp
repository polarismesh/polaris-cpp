//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use
//  this file
//  except in compliance with the License. You may obtain a copy of the License
//  at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the
//  specific
//  language governing permissions and limitations under the License.
//

#include "utils/utils.h"

#include <gtest/gtest.h>
#include <stdlib.h>

#include <string>

#include "plugin/load_balancer/hash/hash_manager.h"

namespace polaris {

TEST(UtilsTest, GetNextSeqId) {
  for (uint32_t i = 0; i < 10; ++i) {
    ASSERT_EQ(Utils::GetNextSeqId(), i);
    ASSERT_EQ(Utils::GetNextSeqId32(), i);
  }
}

TEST(UtilsTest, TestUrlEncodeDecode) {
  std::string url;
  std::string encode_url;
  std::string decode_url;
  std::vector<std::string> urls;
  urls.push_back("");
  urls.push_back("a");
  urls.push_back("ab");
  urls.push_back("abc");
  urls.push_back("service.name");
  for (std::size_t i = 0; i < urls.size(); ++i) {
    url = urls[i];
    encode_url = Utils::UrlEncode(url);
    ASSERT_EQ(encode_url, url);
    decode_url = Utils::UrlDecode(encode_url);
    ASSERT_EQ(url, decode_url);
  }

  urls.clear();
  urls.push_back(" ");
  urls.push_back("%");
  urls.push_back("#service%");
  urls.push_back("#service#name#empty ");
  urls.push_back(" #srv#service#name#");
  urls.push_back("#srv#service#name#instances#");
  for (std::size_t i = 0; i < urls.size(); ++i) {
    url = urls[i];
    encode_url = Utils::UrlEncode(url);
    ASSERT_NE(url, encode_url);
    ASSERT_EQ(url.size() + i * 2, encode_url.size());
    decode_url = Utils::UrlDecode(encode_url);
    ASSERT_EQ(url, decode_url);
  }

  url = "service name";
  encode_url = Utils::UrlEncode(url);
  ASSERT_EQ(url.size(), encode_url.size());
  ASSERT_EQ(encode_url, "service+name");
  decode_url = Utils::UrlDecode(encode_url);
  ASSERT_EQ(url, decode_url);

  url = "service#中文name";  // 中文不转义
  encode_url = Utils::UrlEncode(url);
  ASSERT_EQ(url.size() + 2, encode_url.size());
  decode_url = Utils::UrlDecode(encode_url);
  ASSERT_EQ(url, decode_url);
}

TEST(UtilsTest, TestHashManager) {
  Hash64Func hashFunc = nullptr;
  ASSERT_EQ(HashManager::Instance().GetHashFunction("non_exists_hash", hashFunc), kReturnResourceNotFound);
  ASSERT_EQ(HashManager::Instance().GetHashFunction("murmur3", hashFunc), kReturnOk);
  ASSERT_TRUE(hashFunc != nullptr);
}

}  // namespace polaris
