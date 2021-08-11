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

#include "model/match_string.h"

#include <gtest/gtest.h>

namespace polaris {

TEST(MatchStringTet, InitExactAndRegex) {
  v1::MatchString pb_match_string;
  pb_match_string.mutable_value()->set_value("123");
  pb_match_string.set_type(v1::MatchString::EXACT);
  MatchString match_string;
  ASSERT_TRUE(match_string.Init(pb_match_string));

  pb_match_string.mutable_value()->set_value("((123");
  pb_match_string.set_type(v1::MatchString::REGEX);
  ASSERT_FALSE(match_string.Init(pb_match_string));

  pb_match_string.mutable_value()->set_value("v*");
  ASSERT_TRUE(match_string.Init(pb_match_string));

  pb_match_string.mutable_value()->set_value("^1([0-9][0-9]]$");  // 正则格式错误
  ASSERT_FALSE(match_string.Init(pb_match_string));
}

TEST(MatchStringTet, MatchRegexOr) {
  MatchString match_string;
  v1::MatchString pb_match_string;
  pb_match_string.mutable_value()->set_value("(^84$|1047|1050|1116|50038|50056)");
  pb_match_string.set_type(v1::MatchString::REGEX);
  ASSERT_TRUE(match_string.Init(pb_match_string));
  ASSERT_TRUE(match_string.IsRegex());
  ASSERT_TRUE(match_string.Match("84"));
  ASSERT_FALSE(match_string.Match("1084"));
}

TEST(MatchStringTet, MatchExact) {
  MatchString match_string;
  std::string label = "label";
  v1::MatchString pb_match_string;
  pb_match_string.mutable_value()->set_value(label);
  pb_match_string.set_type(v1::MatchString::EXACT);
  ASSERT_TRUE(match_string.Init(pb_match_string));
  ASSERT_EQ(match_string.GetString(), label);
  ASSERT_TRUE(match_string.IsExactText());
  ASSERT_TRUE(match_string.Match(label));
}

TEST(MatchStringTet, RegexMatch) {
  MatchString match_string;
  v1::MatchString pb_match_string;
  pb_match_string.set_type(v1::MatchString::REGEX);
  pb_match_string.mutable_value()->set_value("^([0-9]|[1-9][0-9])$");
  ASSERT_TRUE(match_string.Init(pb_match_string));
  EXPECT_TRUE(match_string.Match("88"));
  EXPECT_FALSE(match_string.Match("188"));

  pb_match_string.mutable_value()->set_value("^1([0-9][0-9])$");
  ASSERT_TRUE(match_string.Init(pb_match_string));
  EXPECT_FALSE(match_string.Match("88"));
  EXPECT_TRUE(match_string.Match("188"));

  pb_match_string.mutable_value()->set_value("^abcd$");
  ASSERT_TRUE(match_string.Init(pb_match_string));
  EXPECT_FALSE(match_string.Match("abc"));
  EXPECT_TRUE(match_string.Match("abcd"));
  EXPECT_FALSE(match_string.Match("abcef"));
}

TEST(MatchStringTet, MetadataMatch) {
  std::map<std::string, MatchString> rule_metadata;
  std::map<std::string, std::string> metadata;
  ASSERT_TRUE(MatchString::MapMatch(rule_metadata, metadata));
  metadata["k1"] = "v11";
  ASSERT_TRUE(MatchString::MapMatch(rule_metadata, metadata));
  v1::MatchString pb_match_string;
  pb_match_string.mutable_value()->set_value("v1.*");
  pb_match_string.set_type(v1::MatchString::REGEX);
  ASSERT_TRUE(rule_metadata["k1"].Init(pb_match_string));
  ASSERT_TRUE(MatchString::MapMatch(rule_metadata, metadata));

  pb_match_string.mutable_value()->set_value("v2.*");
  ASSERT_TRUE(rule_metadata["k2"].Init(pb_match_string));
  ASSERT_FALSE(MatchString::MapMatch(rule_metadata, metadata));
}

TEST(MatchStringTet, MetadataMatch2) {
  std::map<std::string, MatchString> rule_metadata;
  std::map<std::string, std::string> service_metadata;

  // 空的metadata，匹配成功
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), true);

  // 规则有，数据无，匹配失败
  v1::MatchString match_string;
  match_string.set_type(v1::MatchString::EXACT);
  match_string.mutable_value()->set_value("value");
  rule_metadata["key"].Init(match_string);
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), false);

  service_metadata["other_key"] = "other_value";
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), false);

  service_metadata["key"] = "other_value";
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), false);

  service_metadata["key"] = "value";
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), true);

  match_string.set_type(v1::MatchString::REGEX);
  match_string.mutable_value()->set_value("regex.*");
  rule_metadata["regex_key"].Init(match_string);
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), false);

  service_metadata["regex_key"] = "regex";
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), true);

  service_metadata["regex_key"] = "re";
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), false);

  service_metadata["regex_key"] = "regex_abcd";
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), true);
}

TEST(MatchStringTet, MetadataKeyMatch) {
  std::map<std::string, MatchString> rule_metadata;
  std::map<std::string, std::string> service_metadata;

  v1::MatchString match_string;
  match_string.set_type(v1::MatchString::EXACT);
  match_string.mutable_value()->set_value("base");
  rule_metadata["env"].Init(match_string);
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), false);

  service_metadata["env"] = "test";
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), false);

  service_metadata["env"] = "base";
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), true);

  match_string.set_type(v1::MatchString::REGEX);
  match_string.mutable_value()->set_value("^([0-9]|[1-9][0-9])$");  // key 0-99
  rule_metadata["key"].Init(match_string);
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), false);

  service_metadata["key"] = "88";
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), true);
  service_metadata["key"] = "188";
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), false);

  match_string.set_type(v1::MatchString::REGEX);
  match_string.mutable_value()->set_value("^1([0-9][0-9])$");  // key 100-199
  rule_metadata["key"].Init(match_string);
  service_metadata["key"] = "88";
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), false);
  service_metadata["key"] = "188";
  ASSERT_EQ(MatchString::MapMatch(rule_metadata, service_metadata), true);
}

}  // namespace polaris
