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

#include "config/seed_server.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>
#include "utils/ip_utils.h"

namespace polaris {

TEST(SeedServerConfigTest, TestGetDefaultServers) {
  std::vector<SeedServer> seed_servers;
  std::size_t count = SeedServerConfig::GetDefaultSeedServer(seed_servers);
  ASSERT_EQ(count, seed_servers.size());
  ASSERT_EQ(count, 10);
  for (std::size_t i = 0; i < count; ++i) {
    ASSERT_EQ(seed_servers[i].port_, 8081);
    ASSERT_TRUE(seed_servers[i].ip_[0] == '1');
  }
}

TEST(SeedServerConfigTest, TestParseServers) {
  std::vector<std::string> config_server;
  std::vector<SeedServer> seed_servers;
  ASSERT_EQ(SeedServerConfig::ParseSeedServer(config_server, seed_servers), 0);
  config_server.push_back("only.host");
  ASSERT_EQ(SeedServerConfig::ParseSeedServer(config_server, seed_servers), 0);
  config_server.push_back("host:port");
  ASSERT_EQ(SeedServerConfig::ParseSeedServer(config_server, seed_servers), 0);
  config_server.push_back("host:42");
  ASSERT_EQ(SeedServerConfig::ParseSeedServer(config_server, seed_servers), 1);
}

TEST(SeedServerConfigTest, TestServersToString) {
  std::vector<SeedServer> seed_servers;
  SeedServer server;
  server.ip_   = "123";
  server.port_ = 456;
  seed_servers.push_back(server);
  ASSERT_EQ(SeedServerConfig::SeedServersToString(seed_servers), "123:456");
  server.ip_   = "789";
  server.port_ = 110;
  seed_servers.push_back(server);
  ASSERT_EQ(SeedServerConfig::SeedServersToString(seed_servers), "123:456, 789:110");
}

}  // namespace polaris
