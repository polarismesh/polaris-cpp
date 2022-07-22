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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_NEARBY_ROUTER_H_
#define POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_NEARBY_ROUTER_H_

#include <set>
#include <string>
#include <vector>

#include "cache/service_cache.h"
#include "model/location.h"
#include "plugin/service_router/service_router.h"

namespace polaris {

// 就近路由匹配级别
enum NearbyMatchLevel {
  kNearbyMatchNone = 0,    // 全部不匹配
  kNearbyMatchRegion = 1,  // 只匹配Region
  kNearbyMatchZone = 2,    // 匹配Regin和Zone，默认级别
  kNearbyMatchCampus = 3   // 匹配Regin、Zone和Campus
};

// 就近路由插件配置
class NearbyRouterConfig {
 public:
  NearbyRouterConfig();

  ~NearbyRouterConfig() {}

  bool Init(Config* config);  // 初始化并校验配置

  NearbyMatchLevel GetMatchLevel() const { return match_level_; }

  NearbyMatchLevel GetMaxMatchLevel() const { return max_match_level_; }

  bool IsStrictNearby() const { return strict_nearby_; }

  bool IsEnableDegradeByUnhealthyPercent() const { return enable_degrade_by_unhealthy_percent_; }

  // 有效值为(0, 100]，默认100， 表示全部不健康时才降级
  int GetUnhealthyPercentToDegrade() const { return unhealthy_percent_to_degrade_; }

  bool IsEnableRecoverAll() const { return enable_recover_all_; }

 private:
  bool InitNearbyMatchLevel(Config* config);  // 初始化就近级别配置

  bool InitStrictNearby(Config* config);  // 初始化严格就近配置

  bool InitDegradeConfig(Config* config);  // 初始化降级相关配置

  bool InitRecoverConfig(Config* config);  // 初始化全死全活配置

  static bool StrToMatchLevel(const std::string& str, NearbyMatchLevel& match_level);

 private:
  NearbyMatchLevel match_level_;              // 就近路由匹配级别
  NearbyMatchLevel max_match_level_;          // 允许降级的最大就近级别
  bool strict_nearby_;                        // 是否严格就近
  bool enable_degrade_by_unhealthy_percent_;  // 是否开启按健康比例降级
  int unhealthy_percent_to_degrade_;          // 触发降级的不健康比例
  bool enable_recover_all_;                   // 是否允许全死全活
};

// 用于存储就近级别匹配结果
struct NearbyRouterSet {
  std::vector<Instance*> healthy_;    // 就近级别匹配到的健康实例
  std::vector<Instance*> unhealthy_;  // 就近级别匹配到的不健康实例
};

// 用于存储各个就近级别的匹配结果
class NearbyRouterCluster {
 public:
  explicit NearbyRouterCluster(const NearbyRouterConfig& nearby_router_config);

  // 通过位置信息按就近级别计算就近结果
  void CalculateSet(const Location& location, const std::vector<Instance*>& instances,
                    const std::set<Instance*>& unhealthy_set);

  // 直接将实例按健康和不健康分到同一个就近级别
  void CalculateSet(const std::vector<Instance*>& instances, const std::set<Instance*>& unhealthy_set);

  // 按照就近级别计算最终结果，并返回是否降级
  bool CalculateResult(std::vector<Instance*>& result, int& match_level);

 private:
  friend class NearbyRouterClusterTest_CalculateLocation_Test;

  const NearbyRouterConfig& config_;   // 就近配置
  std::vector<NearbyRouterSet> data_;  // 就近匹配中间结果
};

// 就近路由的实现
class NearbyServiceRouter : public ServiceRouter {
 public:
  NearbyServiceRouter();

  virtual ~NearbyServiceRouter();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual ReturnCode DoRoute(RouteInfo& route_info, RouteResult* route_result);

  virtual RouterStatData* CollectStat();

 private:
  bool CheckLocation();

  static void GetLocationByMatchLevel(const Location& location, int match_level, std::string& level_key,
                                      std::string& level_value);

 private:
  NearbyRouterConfig nearby_router_config_;  // 就近路由配置
  Context* context_;
  ServiceCache<NearbyCacheKey, RouterSubsetCache>* router_cache_;  // 路由结果缓存
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_NEARBY_ROUTER_H_
