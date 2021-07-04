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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_SET_DIVISION_ROUTER_H_
#define POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_SET_DIVISION_ROUTER_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "plugin/plugin_manager.h"
#include "polaris/defs.h"
#include "polaris/plugin.h"

namespace polaris {

class Config;
class Context;
class Instance;
class RouteInfo;
class RouteResult;
struct SetDivisionCacheKey;
template <typename K>
class ServiceCache;

class SetDivisionServiceRouter : public ServiceRouter {
public:
  // 与set信息相关的key:enable_set_key为是否开启set的key，set_name_key为set名的key
  static const char enable_set_key[];
  static const char enable_set_force[];

  SetDivisionServiceRouter();

  virtual ~SetDivisionServiceRouter();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual ReturnCode DoRoute(RouteInfo& route_info, RouteResult* route_result);

  virtual RouterStatData* CollectStat();

  // 根据主调set名和被调set名和metadata判断是否启用set分组
  bool IsSetDivisionRouterEnable(const std::string& caller_set_name,
                                 const std::string& callee_set_name,
                                 const std::map<std::string, std::string>& callee_metadata);

  // 根据主调的set名从被调节点中筛选出满足条件的实例
  // 输入参数:set_name为主调的set名，格式为setname.setarea.setgroupid,
  // src_instances为待筛选的被调节点
  //         wild参数为是否使用通配符，如果wild设置为true，则对应的set_name参数为setname.setarea即可
  // 输出参数:result参数为筛选出来的结果，返回值为0表示成功
  int GetResultWithSetName(std::string set_name, const std::vector<Instance*>& src_instances,
                           std::vector<Instance*>& result, bool wild = false);

  // 根据主调的set名从被调节点中筛选出满足条件的实例，支持通配符，内部调用GetResultWithSetName函数进行匹配
  int CalculateMatchResult(std::string caller_set_name, const std::vector<Instance*>& src_instances,
                           std::vector<Instance*>& result);

  // 根据输入节点集input和unhealthy节点集unhealthy_set，筛选出healthy的节点output
  // 如果input全为unhealthy,则output直接取为input
  int GetHealthyInstances(const std::vector<Instance*>& input,
                          const std::set<Instance*>& unhealthy_set, std::vector<Instance*>& output);

  // 返回互斥的路由插件名称
  const char* GetIncompatibleServiceRouter() { return kPluginNearbyServiceRouter; }

private:
  Context* context_;
  ServiceCache<SetDivisionCacheKey>* router_cache_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_SERVICE_ROUTER_SET_DIVISION_ROUTER_H_
