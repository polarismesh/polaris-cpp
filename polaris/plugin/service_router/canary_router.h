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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_CANARY_ROUTER_H_
#define POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_CANARY_ROUTER_H_

#include <set>
#include <string>
#include <vector>

#include "cache/service_cache.h"
#include "plugin/service_router/service_router.h"

namespace polaris {

// 就近路由的实现
class CanaryServiceRouter : public ServiceRouter {
 public:
  CanaryServiceRouter();

  virtual ~CanaryServiceRouter();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual ReturnCode DoRoute(RouteInfo& route_info, RouteResult* route_result);

  virtual RouterStatData* CollectStat();

  virtual std::string Name();

 private:
  bool CalculateResult(const std::vector<Instance*>& instances, const std::set<Instance*>& unhealthy_set,
                       std::vector<Instance*>& result);

  bool CalculateResult(const std::vector<Instance*>& instances, const std::string& canary_value,
                       const std::set<Instance*>& unhealthy_set, std::vector<Instance*>& result);

 private:
  Context* context_;
  ServiceCache<CanaryCacheKey, RouterSubsetCache>* router_cache_;  // 路由结果缓存
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_CANARY_ROUTER_H_
