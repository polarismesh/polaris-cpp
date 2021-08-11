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

#include "seed_server.h"

#include <stdint.h>
#include <memory>
#include <sstream>
#include <string>

#include "model/constants.h"
#include "utils/ip_utils.h"
#include "utils/string_utils.h"

namespace polaris {

static const int kDefaultPort = 8081;

static const char kDefaultSeedServerName[] = "default";
static const uint32_t kDefaultSeedServer[] = {16777343};
static const int kDefaultSeedServerSize    = sizeof(kDefaultSeedServer) / sizeof(uint32_t);

// 内置Polaris Server服务
static const char kDiscoverServiceDefault[]          = "polaris.discover";
static const char kHealthCheckServiceDefault[]       = "polaris.healthcheck";
static const char kMonitorServiceDefault[]           = "polaris.monitor";
static const char kMetricServiceDefault[]            = "";
static const uint64_t kPolarisRefreshIntervalDefault = 10 * 60 * 1000;

PolarisCluster::PolarisCluster(std::string service) {
  service_.namespace_ = constants::kPolarisNamespace;
  service_.name_      = service;
  refresh_interval_   = kPolarisRefreshIntervalDefault;
}

void PolarisCluster::Update(const std::string service_namespace, const std::string& service_name) {
  service_.namespace_ = service_namespace;
  service_.name_      = service_name;
}

SeedServerConfig::SeedServerConfig()
    : seed_server_(kDefaultSeedServerName), discover_cluster_(kDiscoverServiceDefault),
      heartbeat_cluster_(kHealthCheckServiceDefault), monitor_cluster_(kMonitorServiceDefault),
      metric_cluster_(kMetricServiceDefault) {}

ReturnCode SeedServerConfig::UpdateJoinPoint(std::string join_point) {
  if (StringUtils::IgnoreCaseCmp(join_point, kDefaultSeedServerName)) {  // 国内默认集群
    seed_server_ = kDefaultSeedServerName;
    discover_cluster_.Update(constants::kPolarisNamespace, kDiscoverServiceDefault);
    heartbeat_cluster_.Update(constants::kPolarisNamespace, kHealthCheckServiceDefault);
    monitor_cluster_.Update(constants::kPolarisNamespace, kMonitorServiceDefault);
  } else {
    return kReturnInvalidConfig;
  }
  return kReturnOk;
}

std::size_t SeedServerConfig::GetSeedServer(std::vector<SeedServer>& seed_servers) {
  if (seed_server_ == kDefaultSeedServerName) {
    GetDefaultSeedServer(seed_servers);
  }
  return seed_servers.size();
}

std::size_t SeedServerConfig::GetDefaultSeedServer(std::vector<SeedServer>& seed_servers) {
  SeedServer server;
  server.port_ = kDefaultPort;
  for (int i = 0; i < kDefaultSeedServerSize; i++) {
    if (IpUtils::IntIpToStr(kDefaultSeedServer[i], server.ip_)) {
      seed_servers.push_back(server);
    }
  }
  return seed_servers.size();
}

std::size_t SeedServerConfig::ParseSeedServer(const std::vector<std::string>& config_servers,
                                              std::vector<SeedServer>& seed_servers) {
  SeedServer server;
  for (std::size_t i = 0; i < config_servers.size(); ++i) {
    const std::string& str_server = config_servers[i];
    std::size_t split             = str_server.find(':');
    if (split != std::string::npos) {
      server.ip_ = str_server.substr(0, split);
      if (!StringUtils::SafeStrToType(str_server.substr(split + 1), server.port_)) {
        continue;
      }
      seed_servers.push_back(server);
    }
  }
  return seed_servers.size();
}

std::string SeedServerConfig::SeedServersToString(std::vector<SeedServer>& seed_servers) {
  std::ostringstream output;
  for (std::size_t i = 0; i < seed_servers.size(); ++i) {
    if (i > 0) {
      output << ", ";
    }
    output << seed_servers[i].ip_ << ":" << seed_servers[i].port_;
  }
  return output.str();
}

}  // namespace polaris
