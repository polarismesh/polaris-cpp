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

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <string>
#include <vector>

#include "test_utils.h"
#include "utils/file_utils.h"

#include "polaris/config.h"

namespace polaris {

class ConfigTest : public ::testing::Test {
 protected:
  virtual void SetUp() {}

  virtual void TearDown() {
    if (config_ != nullptr) {
      delete config_;
    }
  }

  Config *config_;
  std::string content_;
  std::string err_msg_;
};

// 从文件创建
TEST_F(ConfigTest, TestCreateConfigFromFile) {
  // 从不存在的文件创建配置失败
  config_ = Config::CreateFromFile("not_exist.file", err_msg_);
  ASSERT_FALSE(err_msg_.empty());
  ASSERT_TRUE(config_ == nullptr);

  // 创建临时文件
  std::string temp_file;
  TestUtils::CreateTempFile(temp_file);

  // 从该临时文件创建配置成功
  config_ = Config::CreateFromFile(temp_file, err_msg_);
  ASSERT_TRUE(config_ != nullptr);

  FileUtils::RemoveFile(temp_file);
}

// 从字符串创建
TEST_F(ConfigTest, TestCreateConfigFromString) {
  // 从非法的字符串创建配置失败
  config_ = Config::CreateFromString("[,,,", err_msg_);
  ASSERT_FALSE(err_msg_.empty());
  ASSERT_TRUE(config_ == nullptr);

  std::string content = "{int: 1, string: string, string_seq: [seq1, seq2], path1.path2: file}";
  // 从该合法字符串创建成功，并能成功读取到数据
  err_msg_.clear();
  config_ = Config::CreateFromString(content, err_msg_);
  ASSERT_TRUE(config_ != nullptr);
  ASSERT_TRUE(err_msg_.empty());
  ASSERT_EQ(config_->GetIntOrDefault("int", -1), 1);
  ASSERT_EQ(config_->GetStringOrDefault("string", ""), "string");
  std::vector<std::string> seq = config_->GetListOrDefault("string_seq", "");
  ASSERT_EQ(seq.size(), 2);
  ASSERT_EQ(seq[0], "seq1");
  ASSERT_EQ(seq[1], "seq2");
  ASSERT_EQ(config_->GetStringOrDefault("path1.path2", ""), "file");
}

// 创建空配置
TEST_F(ConfigTest, TestCreateEmptyConfig) {
  config_ = Config::CreateEmptyConfig();
  ASSERT_TRUE(config_ != nullptr);

  Config *another_config = Config::CreateEmptyConfig();
  ASSERT_TRUE(another_config != nullptr);

  // 两次返回的对象必须不一样，这样对其进行修改不会相互影响
  ASSERT_TRUE(config_ != another_config);
  delete another_config;
}

// 测试获取子配置
TEST_F(ConfigTest, TestGetSubConfig) {
  content_ =
      "{\"root\": {\"sub1\": {\"key1\": \"value11\", \"key2\": \"value12\"}, "
      "\"sub2\": {\"key1\": \"value21\"}}}";
  config_ = Config::CreateFromString(content_, err_msg_);
  ASSERT_TRUE(config_ != nullptr);
  ASSERT_TRUE(err_msg_.empty());

  Config *sub_config = config_->GetSubConfig("root");
  ASSERT_TRUE(sub_config != nullptr);

  Config *sub1 = sub_config->GetSubConfig("sub1");
  ASSERT_TRUE(sub1 != nullptr);
  EXPECT_EQ(sub1->GetStringOrDefault("key1", ""), "value11");
  EXPECT_EQ(sub1->GetStringOrDefault("key2", ""), "value12");
  delete sub1;

  Config *sub2 = sub_config->GetSubConfig("sub2");
  ASSERT_TRUE(sub2 != nullptr);
  EXPECT_EQ(sub2->GetStringOrDefault("key1", ""), "value21");
  delete sub2;
  delete sub_config;

  EXPECT_EQ(config_->ToJsonString(), content_);
  EXPECT_TRUE(!config_->ToString().empty());
}

TEST_F(ConfigTest, TestGetEmptyConfig) {
  config_ = Config::CreateEmptyConfig();

  ASSERT_EQ(config_->GetBoolOrDefault("bool", true), true);
  ASSERT_FLOAT_EQ(config_->GetFloatOrDefault("float", 1.2), 1.2);
  ASSERT_EQ(config_->GetIntOrDefault("int", 42), 42);
  std::vector<std::string> vec = config_->GetListOrDefault("list", "1,2");
  ASSERT_TRUE(vec.size() == 2 && vec[0] == "1" && vec[1] == "2");
  ASSERT_EQ(config_->GetMsOrDefault("time", 100), 100);
  ASSERT_EQ(config_->GetStringOrDefault("string", "value"), "value");

  Config *sub_config = config_->GetSubConfig("sub_config");
  ASSERT_TRUE(sub_config != nullptr);
  delete sub_config;
}

TEST_F(ConfigTest, TestGetStringOrDefault) {
  content_ =
      "int:\n"
      "  1\n"
      "float:\n"
      "  1.1\n"
      "bool:\n"
      "  true\n"
      "string:\n"
      "  value\n"
      "list:\n"
      "  - 1\n"
      "  - 2\n"
      "str_list: 1,2";
  config_ = Config::CreateFromString(content_, err_msg_);
  ASSERT_TRUE(config_ != nullptr);
  ASSERT_TRUE(err_msg_.empty());

  EXPECT_EQ(config_->GetStringOrDefault("int", ""), "1");
  EXPECT_EQ(config_->GetStringOrDefault("float", ""), "1.1");
  EXPECT_EQ(config_->GetStringOrDefault("bool", ""), "true");
  EXPECT_EQ(config_->GetStringOrDefault("string", ""), "value");
  EXPECT_EQ(config_->GetStringOrDefault("str_list", ""), "1,2");

  // 序列无法转换成String返回
  EXPECT_THROW(config_->GetStringOrDefault("list", ""), YAML_0_3::InvalidScalar);
  // 不存在的key返回默认值
  ASSERT_EQ(config_->GetStringOrDefault("not_exist_key", "test"), "test");
}

TEST_F(ConfigTest, ToStringWithDefaultValue) {
  content_ =
      "key1:\n"
      "  42";
  config_ = Config::CreateFromString(content_, err_msg_);
  ASSERT_TRUE(config_ != nullptr);
  ASSERT_TRUE(err_msg_.empty());

  EXPECT_EQ(config_->GetStringOrDefault("key1", "default"), "42");
  EXPECT_EQ(config_->GetStringOrDefault("key2", "default"), "default");

  EXPECT_EQ(config_->ToString(), "key1: 42\nkey2: default");
  EXPECT_EQ(config_->ToJsonString(), "{\"key1\": \"42\", \"key2\": \"default\"}");
}

TEST_F(ConfigTest, TestGetIntOrDefault) {
  content_ =
      "int1:\n"
      "  100\n"
      "int2:\n"
      "  -200\n"
      "str:\n"
      "  value";
  config_ = Config::CreateFromString(content_, err_msg_);
  ASSERT_TRUE(config_ != nullptr);
  ASSERT_TRUE(err_msg_.empty());

  EXPECT_EQ(config_->GetIntOrDefault("int1", 0), 100);
  EXPECT_EQ(config_->GetIntOrDefault("int2", 0), -200);

  // 不存在的key返回默认值
  ASSERT_EQ(config_->GetIntOrDefault("not_exist_key", 100), 100);

  EXPECT_EQ(config_->ToString(), "int1: 100\nint2: -200\nnot_exist_key: 100");
  EXPECT_EQ(config_->ToJsonString(), "{\"int1\": 100, \"int2\": -200, \"not_exist_key\": 100}");

  // String无法转换
  EXPECT_THROW(config_->GetIntOrDefault("str", 0), YAML_0_3::InvalidScalar);
}

TEST_F(ConfigTest, TestGetBoolOrDefault) {
  content_ =
      "bool1:\n"
      "  true\n"
      "bool2:\n"
      "  false\n"
      "int:\n"
      "  100\n"
      "string:\n"
      "  value";
  config_ = Config::CreateFromString(content_, err_msg_);
  ASSERT_TRUE(config_ != nullptr);
  ASSERT_TRUE(err_msg_.empty());

  EXPECT_EQ(config_->GetBoolOrDefault("bool1", false), true);
  EXPECT_EQ(config_->GetBoolOrDefault("bool2", true), false);

  // 不存在的key返回默认值
  EXPECT_EQ(config_->GetBoolOrDefault("not_exist_key", false), false);
  EXPECT_EQ(config_->GetBoolOrDefault("not_exist_key2", true), true);

  EXPECT_EQ(config_->ToString(), "bool1: true\nbool2: false\nnot_exist_key: false\nnot_exist_key2: true");
  EXPECT_EQ(config_->ToJsonString(),
            "{\"bool1\": true, \"bool2\": false,"
            " \"not_exist_key\": false, \"not_exist_key2\": true}");

  // Int 和 String无法转换
  EXPECT_THROW(config_->GetBoolOrDefault("int", false), YAML_0_3::InvalidScalar);
  EXPECT_THROW(config_->GetBoolOrDefault("string", false), YAML_0_3::InvalidScalar);
}

TEST_F(ConfigTest, TestGetFloatOrDefault) {
  content_ =
      "float1:\n"
      "  0.8\n"
      "float2:\n"
      "  1.2\n"
      "int:\n"
      "  1\n"
      "string:\n"
      "  value";
  config_ = Config::CreateFromString(content_, err_msg_);
  ASSERT_TRUE(config_ != nullptr);
  ASSERT_TRUE(err_msg_.empty());

  EXPECT_FLOAT_EQ(config_->GetFloatOrDefault("float1", 0), 0.8);
  EXPECT_FLOAT_EQ(config_->GetFloatOrDefault("float2", 0), 1.2);
  EXPECT_FLOAT_EQ(config_->GetFloatOrDefault("int", 0), 1.0);
  // 不存在的key返回默认值
  ASSERT_FLOAT_EQ(config_->GetFloatOrDefault("not_exist_key", 0.11), 0.11);

  EXPECT_EQ(config_->ToString(), "float1: 0.8\nfloat2: 1.2\nint: 1\nnot_exist_key: 0.11");
  EXPECT_EQ(config_->ToJsonString(),
            "{\"float1\": 0.8, \"float2\": 1.2, "
            "\"int\": 1, \"not_exist_key\": 0.11}");

  // String无法转换
  EXPECT_THROW(config_->GetFloatOrDefault("string", 0.5), YAML_0_3::InvalidScalar);
}

// 测试各种时间配置能够正确读取
TEST_F(ConfigTest, TestGetMsOrDefault) {
  content_ =
      "hour:\n"
      "  2h\n"
      "minute:\n"
      "  2m\n"
      "second:\n"
      "  2s\n"
      "mill.second:\n"
      "  2ms\n"
      "int:\n"
      "  100\n"
      "string:\n"
      "  value\n"
      "negative:\n"
      "  -100";
  config_ = Config::CreateFromString(content_, err_msg_);
  ASSERT_TRUE(config_ != nullptr);
  ASSERT_TRUE(err_msg_.empty());

  ASSERT_EQ(config_->GetMsOrDefault("hour", 0), 2 * 60 * 60 * 1000);
  ASSERT_EQ(config_->GetMsOrDefault("minute", 0), 2 * 60 * 1000);
  ASSERT_EQ(config_->GetMsOrDefault("second", 0), 2 * 1000);
  ASSERT_EQ(config_->GetMsOrDefault("mill.second", 0), 2 * 1);
  // Int可以转换
  ASSERT_EQ(config_->GetMsOrDefault("int", 0), 100);
  // 不存在的key返回默认值
  ASSERT_FLOAT_EQ(config_->GetMsOrDefault("not_exist_key", 1000), 1000);

  EXPECT_EQ(config_->ToString(),
            "hour: 2h\nminute: 2m\nsecond: "
            "2s\nmill.second: 2ms\nint: "
            "100\nnot_exist_key: 1000");
  EXPECT_EQ(config_->ToJsonString(),
            "{\"hour\": \"2h\", \"minute\": \"2m\", \"second\": \"2s\","
            " \"mill.second\": \"2ms\", \"int\": \"100\", \"not_exist_key\": 1000}");

  // String无法转换
  EXPECT_THROW(config_->GetMsOrDefault("string", 20), YAML_0_3::InvalidScalar);
  // 负数
  EXPECT_THROW(config_->GetMsOrDefault("negative", 500), YAML_0_3::InvalidScalar);
}

TEST_F(ConfigTest, TestGetListOrDefault) {
  content_ =
      "list1:\n"
      "  - 1\n"
      "  - 2\n"
      "list2: [3, 4]\n"
      "string: value";
  config_ = Config::CreateFromString(content_, err_msg_);
  ASSERT_TRUE(config_ != nullptr);
  ASSERT_TRUE(err_msg_.empty());

  std::vector<std::string> list;
  list = config_->GetListOrDefault("list1", "");
  ASSERT_EQ(list.size(), 2);
  ASSERT_EQ(list[0], "1");
  ASSERT_EQ(list[1], "2");

  list = config_->GetListOrDefault("list2", "");
  ASSERT_EQ(list.size(), 2);
  ASSERT_EQ(list[0], "3");
  ASSERT_EQ(list[1], "4");

  // String无法转换
  list = config_->GetListOrDefault("string", "4 ,5");
  ASSERT_EQ(list.size(), 0);

  // 获取不存在的Key
  list = config_->GetListOrDefault("not_exist_key0", "");
  ASSERT_TRUE(list.empty());
  list = config_->GetListOrDefault("not_exist_key1", "0");
  ASSERT_EQ(list.size(), 1);
  ASSERT_EQ(list[0], "0");
  list = config_->GetListOrDefault("not_exist_key2", " 1 ");
  ASSERT_EQ(list.size(), 1);
  ASSERT_EQ(list[0], "1");
  list = config_->GetListOrDefault("not_exist_key3", "2,3");
  ASSERT_EQ(list.size(), 2);
  ASSERT_EQ(list[0], "2");
  ASSERT_EQ(list[1], "3");
  list = config_->GetListOrDefault("not_exist_key4", " 4 , 5 ");
  ASSERT_EQ(list.size(), 2);
  ASSERT_EQ(list[0], "4");
  ASSERT_EQ(list[1], "5");

  EXPECT_EQ(config_->ToString(),
            "list1:\n  - 1\n  - 2\nlist2:\n  - 3\n  - 4\nstring:\n  "
            "[]\nnot_exist_key0:\n  []\n"
            "not_exist_key1:\n  - 0\nnot_exist_key2:\n  - 1\nnot_exist_key3:\n "
            " - 2\n  - 3\nnot_exist_key4:\n  - 4\n  - 5");
  EXPECT_EQ(config_->ToJsonString(),
            "{\"list1\": [\"1\", \"2\"], \"list2\": [\"3\", \"4\"], \"string\": [], "
            "\"not_exist_key0\": [], \"not_exist_key1\": [\"0\"], "
            "\"not_exist_key2\": [\"1\"], "
            "\"not_exist_key3\": [\"2\", \"3\"], \"not_exist_key4\": [\"4\", "
            "\"5\"]}");
}

TEST_F(ConfigTest, TestGetMap) {
  content_ =
      "map1:\n"
      "  k1: v1\n"
      "  k2: v2\n"
      "map2:\n"
      "  k1: v1\n"
      "map3:\n"
      "  k1";
  config_ = Config::CreateFromString(content_, err_msg_);
  ASSERT_TRUE(config_ != nullptr) << err_msg_;
  ASSERT_TRUE(err_msg_.empty());

  std::map<std::string, std::string> map;
  map = config_->GetMap("map0");
  ASSERT_TRUE(map.empty());

  map = config_->GetMap("map1");
  ASSERT_EQ(map.size(), 2);
  ASSERT_EQ(map["k1"], "v1");
  ASSERT_EQ(map["k2"], "v2");

  map = config_->GetMap("map2");
  ASSERT_EQ(map.size(), 1);
  ASSERT_EQ(map["k1"], "v1");

  map = config_->GetMap("map3");
  ASSERT_TRUE(map.empty());

  EXPECT_EQ(config_->ToString(), "map0:\n  {}\nmap1:\n  k1: v1\n  k2: v2\nmap2:\n  k1: v1\nmap3:\n  {}");
  EXPECT_EQ(config_->ToJsonString(),
            "{\"map0\": {}, \"map1\": {\"k1\": \"v1\", \"k2\": \"v2\"}, \"map2\": {\"k1\": "
            "\"v1\"}, \"map3\": {}}");
}

TEST_F(ConfigTest, TestSubConfig) {
  content_ = R"###(
service:
  - name: service.name1  # 服务名
    namespace: Test      # 服务所属命名空间
    serviceRouter:       # 服务级路由配置
      plugin:
        nearbyBasedRouter:
          matchLevel: campus
  - name: service.name2  # 服务名
    namespace: Test      # 服务所属命名空间
    loadBalancer:        # 服务级负载均衡配置
      type: ringHash
      vnodeCount: 10240
)###";

  config_ = Config::CreateFromString(content_, err_msg_);
  ASSERT_TRUE(config_ != nullptr) << err_msg_;
  ASSERT_TRUE(err_msg_.empty());

  auto service_config = config_->GetSubConfigList("service");
  for (auto item : service_config) {
    ASSERT_TRUE(item->GetStringOrDefault("name", "").find("service.name") != std::string::npos);
    ASSERT_EQ(item->GetStringOrDefault("namespace", ""), "Test");
    delete item;
  }
  ASSERT_FALSE(config_->ToString().empty());
}

TEST_F(ConfigTest, TestSubConfigExist) {
  content_ = R"###(
loadBalancer:
  type: ringHash
  vnodeCount: 10240
)###";

  config_ = Config::CreateFromString(content_, err_msg_);
  ASSERT_TRUE(config_ != nullptr) << err_msg_;
  ASSERT_TRUE(err_msg_.empty());

  ASSERT_TRUE(config_->SubConfigExist("loadBalancer"));
  ASSERT_FALSE(config_->SubConfigExist("circuitBreaker"));
  auto sub_config = config_->GetSubConfig("loadBalancer");
  ASSERT_TRUE(sub_config->SubConfigExist("vnodeCount"));
  delete sub_config;
}

}  // namespace polaris
