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

/// @file defs.h
/// @brief 本文件定义返回码和一些基本类型
///
#ifndef POLARIS_CPP_INCLUDE_POLARIS_DEFS_H_
#define POLARIS_CPP_INCLUDE_POLARIS_DEFS_H_

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

#include <map>
#include <string>

namespace polaris {

/// @brief 接口返回错误码
enum ReturnCode {
  kReturnOk           = 0,     ///< 成功
  kReturnUnknownError = 1000,  ///< 未知错误
  kReturnInvalidArgument = 1001,  ///< 参数非法，客户端和服务端都会检查参数的正确性
  kReturnInvalidConfig     = 1002,  ///< 配置不正确
  kReturnPluginError       = 1003,  ///< 插件获取相关错误
  kReturnTimeout           = 1004,  ///< 请求超时
  kReturnInvalidState      = 1005,  ///< 程序状态非法错误
  kReturnServerError       = 1006,  ///< 服务调用返回错误
  kReturnNetworkFailed     = 1007,  ///< 网络调用错误
  kReturnInstanceNotFound  = 1010,  ///< 服务实例不存在
  kReturnInvalidRouteRule  = 1011,  ///< 路由规则非法
  kReturnRouteRuleNotMatch = 1012,  ///< 路由规则匹配失败
  kReturnServiceNotFound   = 1015,  ///< 服务不存在
  kReturnExistedResource = 1200,  ///< 资源已存在，用于服务实例重复注册的返回码
  kReturnUnauthorized    = 1201,  ///< 请求未授权，token错误
  kReturnHealthyCheckDisable        = 1202,  ///< 服务端或实例健康检查未开启
  kRetrunRateLimit                  = 1203,  ///< 请求被限频
  kReturnNotInit                    = 1288,  ///< 资源未初始化
  kReturnResourceNotFound           = 1289,  // 资源未找到
  kReturnServerUnknownError         = 1299,  ///< 服务端返回客户端未知的错误
  kReturnSystemServiceNotConfigured = 1300,  ///< 没有配置系统服务名字
};

/// @brief 返回码转换为字符串消息
///
/// @param return_code 需要转换的返回码
/// @return std::string 返回码对应的字符串信息
std::string ReturnCodeToMsg(ReturnCode return_code);

/// @组合service的namespace和name用于唯一确定一个服务
struct ServiceKey {
  std::string namespace_;
  std::string name_;
};

/// @brief 实现比较函数用于ServiceKey作为map的key
bool operator<(ServiceKey const &lhs, ServiceKey const &rhs);

bool operator==(const ServiceKey &lhs, const ServiceKey &rhs);

/// @brief 定义源服务信息，用于服务路由执行过滤
struct ServiceInfo {
  ServiceKey service_key_;
  std::map<std::string, std::string> metadata_;
};

/// @brief 负载均衡类型
///
/// @note 添加负载均衡插件时必须添加一个类型,并定义该插件的 GetLoadBalanceType 方法返回该值
typedef std::string LoadBalanceType;
// 权重随机
const static LoadBalanceType kLoadBalanceTypeWeightedRandom = "weightedRandom";
// 一致性hash负载均衡
const static LoadBalanceType kLoadBalanceTypeRingHash = "ringHash";
// 一致性Hash: maglev 算法
const static LoadBalanceType kLoadBalanceTypeMaglevHash = "maglev";
// 兼容L5的一致性Hash
const static LoadBalanceType kLoadBalanceTypeL5CstHash = "l5cst";
// hash_key%总实例数 选择服务实例
const static LoadBalanceType kLoadBalanceTypeSimpleHash = "simpleHash";
// 兼容brpc c_murmur的一致性哈希
const static LoadBalanceType kLoadBalanceTypeCMurmurHash = "cMurmurHash";
// 兼容brpc locality_aware的负载均衡
const static LoadBalanceType kLoadBalanceTypeLocalityAware = "localityAware";
// 使用全局配置的负载均衡算法
const static LoadBalanceType kLoadBalanceTypeDefaultConfig = "default";

/// @brief 元数据路由匹配失败时降级策略
enum MetadataFailoverType {
  kMetadataFailoverNone,    // 默认不降级
  kMetadataFailoverAll,     // 降级返回所有节点
  kMetadataFailoverNotKey,  // 返回不包含元数据路由key的节点
};

struct MetadataRouterParam {
  MetadataRouterParam() : failover_type_(kMetadataFailoverNone) {}
  std::map<std::string, std::string> metadata_;
  MetadataFailoverType failover_type_;
};

/// @brief 负载均衡参数
struct Criteria {
  Criteria() : hash_key_(0), ignore_half_open_(false), replicate_index_(0) {}
  uint64_t hash_key_;
  bool ignore_half_open_;
  std::string hash_string_;
  int replicate_index_;
};

/// @brief 服务实例状态，用于上报服务实例状态
///
/// 服务实例超时和错误会用于实例熔断 @note 调用服务实例返回业务错误不要上报错误
enum CallRetStatus {
  kCallRetOk = 0,   ///< 服务实例正常
  kCallRetTimeout,  ///< 服务实例超时
  kCallRetError     ///< 服务实例错误
};

/// @brief 三级位置信息
struct Location {
  std::string region;
  std::string zone;
  std::string campus;
};

/// @brief 权重类型
enum WeightType {
  kStaticWeightType = 0,  ///< 静态权重，实例注册或用户在控制台设置的实例权重
  kDynamicWeightType      ///< 动态权重，权重调整插件计算的权重
};

/// @brief 健康检查类型
enum HealthCheckType {
  kHeartbeatHealthCheck = 0,  ///< 根据心跳上报进行健康检查
};

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_DEFS_H_
