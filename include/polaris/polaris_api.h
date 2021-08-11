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

// Polaris C接口头文件

#ifndef POLARIS_CPP_INCLUDE_POLARIS_POLARIS_API_H_
#define POLARIS_CPP_INCLUDE_POLARIS_POLARIS_API_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////

/// @brief 设置日志路径
///
/// @param log_dir 日志路径
void polaris_set_log_dir(const char* log_dir);

/// @brief 北极星日志级别
typedef enum PolarisLogLevel {
  kPolarisLogLevelTrace = 0,
  kPolarisLogLevelDebug,
  kPolarisLogLevelInfo,
  kPolarisLogLevelWarn,
  kPolarisLogLevelError,
  kPolarisLogLevelFatal
} PolarisLogLevel;

/// @brief 设置北极星默认日志级别
///
/// @param log_level 日志级别
void polaris_set_log_level(PolarisLogLevel log_level);

/// @brief 通过错误码获取错误信息
///
/// @param ret_code 错误码
/// @return const char* 错误信息
const char* polaris_get_err_msg(int ret_code);

///////////////////////////////////////////////////////////////////////////////

/// @brief API对象，所有接口必须基于polaris_api使用
typedef struct _polaris_api polaris_api;

/// @brief 通过当前路径下的polaris.yml配置创建API对象，如果该配置文件不存在则使用默认配置。
///
/// @return polaris_api* API对象
polaris_api* polaris_api_new(void);

/// @brief 通过配置文件创建API对象
///
/// @param config_file 配置文件
/// @return polaris_api* API对象
polaris_api* polaris_api_new_from(const char* config_file);

/// @brief 通过配置内容创建API对象
///
/// @param content 配置内容
/// @return polaris_api* API对象
polaris_api* polaris_api_new_from_content(const char* content);

/// @brief 销毁API对象
///
/// @param api API对象
void polaris_api_destroy(polaris_api** api);

///////////////////////////////////////////////////////////////////////////////

/// @brief 获取单个服务实例请求
typedef struct _polaris_get_one_instance_req polaris_get_one_instance_req;

/// @brief 创建获取单个服务实例请求对象
///
/// @param service_namespace 服务命名空间
/// @param service_name 服务名
/// @return polaris_get_one_instance_req* 获取单个服务实例请求
polaris_get_one_instance_req* polaris_get_one_instance_req_new(const char* service_namespace,
                                                               const char* service_name);

/// @brief 销毁获取单个服务实例请求对象
///
/// @param get_one_instance_req 获取单个请求服务实例请求
void polaris_get_one_instance_req_destroy(polaris_get_one_instance_req** get_one_instance_req);

/// @brief 设置hash key，用于一致性哈希负载均衡算法选择服务实例。其他负载均衡算法不用设置。可选
///
/// @param get_one_instance_req 获取单个请求服务实例请求
/// @param hash_key 业务自己hash得到的key
void polaris_get_one_instance_req_set_hash_key(polaris_get_one_instance_req* get_one_instance_req,
                                               uint64_t hash_key);

/// @brief 设置hash字符串。用于一致性哈希负载均衡算法选择服务实例。其他负载均衡算法不用设置。可选
/// @note sdk会用hash_string算出一个uint64_t的key，业务无需hash
///
/// @param get_one_instance_req 获取单个请求服务实例请求
/// @param hash_string 用于hash的字符串
void polaris_get_one_instance_req_set_hash_string(
    polaris_get_one_instance_req* get_one_instance_req, const char* hash_string);

/// @brief 设置是否略过跳过半开探测节点。默认分配半开节点用于熔断节点恢复
/// @note 只在重试业务时设置为true。注意不能全部请求都设置为true，否则熔断节点一直无法恢复
///
/// @param get_one_instance_req 获取单个请求服务实例请求
/// @param ignore_half_open
void polaris_get_one_instance_req_set_ignore_half_open(
    polaris_get_one_instance_req* get_one_instance_req, bool ignore_half_open);

/// @brief 设置源服务用于匹配规则路由。可选
///
/// @param get_one_instance_req 获取单个请求服务实例请求
/// @param service_namespace 源服务命名空间
/// @param service_name 源服务服务名
void polaris_get_one_instance_req_set_src_service_key(
    polaris_get_one_instance_req* get_one_instance_req, const char* service_namespace,
    const char* service_name);

/// @brief 添加源服务请求元数据用于匹配路由规则。可选
///
/// @param get_one_instance_req 获取单个请求服务实例请求
/// @param item_name 元数据名字
/// @param item_value 元数据值
void polaris_get_one_instance_req_add_src_service_metadata(
    polaris_get_one_instance_req* get_one_instance_req, const char* item_name,
    const char* item_value);

/// @brief 设置调用哪个set下的服务
///
/// @param get_one_instance_req 获取单个请求服务实例请求
/// @param set_name 主调指定的被调服务的set名
void polaris_get_one_instance_req_set_src_set_name(
    polaris_get_one_instance_req* get_one_instance_req, const char* set_name);

/// @brief 设置调用哪个金丝雀服务实例
///
/// @param get_one_instance_req 获取单个服务实例请求
/// @param canary  主调指定的用于选择被调的金丝雀
void polaris_get_one_instance_req_set_canary(polaris_get_one_instance_req* get_one_instance_req,
                                             const char* canary);

/// @brief 设置请求的超时时间。可选，不设置默认为1s
///
/// @param get_one_instance_req 获取单个服务实例请求
/// @param timeout
void polaris_get_one_instance_req_set_timeout(polaris_get_one_instance_req* get_one_instance_req,
                                              uint64_t timeout);

/// @brief 设置元数据路由插件的元数据参数
///
/// @param get_one_instance_req 获取单个服务实例请求
/// @param item_name 元数据名字
/// @param item_value 元数据值
void polaris_get_one_instance_req_metadata_add_item(
    polaris_get_one_instance_req* get_one_instance_req, const char* item_name,
    const char* item_value);

/// @brief 元数据路由插件的降级策略
typedef enum PolarisMetadataFailoverType {
  kPolarisMetadataFailoverNone,    // 默认不降级
  kPolarisMetadataFailoverAll,     // 降级返回所有节点
  kPolarisMetadataFailoverNotKey,  // 返回不包含元数据路由key的节点
} PolarisMetadataFailoverType;

/// @brief 设置元数据路由插件的降级策略
///
/// @param get_one_instance_req 获取单个服务实例请求
/// @param failover_type 降级策略
void polaris_get_one_instance_req_metadata_failover(
    polaris_get_one_instance_req* get_one_instance_req, PolarisMetadataFailoverType failover_type);

///////////////////////////////////////////////////////////////////////////////

/// @brief 批量获取服务实例请求
typedef struct _polaris_get_instances_req polaris_get_instances_req;

/// @brief 创建批量获取服务实例请求
///
/// @param service_namespace 服务命名空间
/// @param service_name 服务名
/// @return polaris_get_instances_req* 批量获取服务实例请求
polaris_get_instances_req* polaris_get_instances_req_new(const char* service_namespace,
                                                         const char* service_name);

/// @brief 销毁批量获取服务实例请求
///
/// @param get_instances_req 批量获取服务实例请求
void polaris_get_instances_req_destroy(polaris_get_instances_req** get_instances_req);

/// @brief 设置源服务用于匹配规则路由。可选
///
/// @param get_instances_req 批量获取服务实例请求
/// @param service_namespace 源服务命名空间
/// @param service_name 源服务服务名
void polaris_get_instances_req_set_src_service_key(polaris_get_instances_req* get_instances_req,
                                                   const char* service_namespace,
                                                   const char* service_name);

/// @brief 添加元数据用于匹配路由规则。可选
///
/// @param get_instances_req 批量获取服务实例请求
/// @param item_name 元数据名字
/// @param item_value 元数据值
void polaris_get_instances_req_add_src_service_metadata(
    polaris_get_instances_req* get_instances_req, const char* item_name, const char* item_value);

/// @brief 设置结果中是否要包含不健康实例
///
/// @param get_instances_req 批量获取服务实例请求
/// @param include_unhealthy_instances 是否包含不健康实例
void polaris_get_instances_req_include_unhealthy(polaris_get_instances_req* get_instances_req,
                                                 bool include_unhealthy_instances);

/// @brief 设置结果中是否要包含熔断实例
///
/// @param get_instances_req 批量获取服务实例请求
/// @param include_circuit_breaker_instances 是否包含熔断实例
void polaris_get_instances_req_include_circuit_break(polaris_get_instances_req* get_instances_req,
                                                     bool include_circuit_breaker_instances);

/// @brief 是否跳过服务路由
///
/// @param get_instances_req 批量获取服务实例请求
/// @param skip_route_filter 是否跳过服务路由
void polaris_get_instances_req_skip_route_filter(polaris_get_instances_req* get_instances_req,
                                                 bool skip_route_filter);

/// @brief 设置超时时间。可选，不设置默认为1s
///
/// @param get_instances_req 批量获取服务实例请求
/// @param timeout 超时时间
void polaris_get_instances_req_set_timeout(polaris_get_instances_req* get_instances_req,
                                           uint64_t timeout);

/// @brief 设置选择哪个金丝雀下的服务实例
///
/// @param get_instances_req 批量获取服务实例请求
/// @param canary 金丝雀名字
void polaris_get_instances_req_set_canary(polaris_get_instances_req* get_instances_req,
                                          const char* canary);

///////////////////////////////////////////////////////////////////////////////

/// @brief 服务实例
typedef struct _polaris_instance polaris_instance;

/// @brief 销毁服务实例
///
/// @param instance 服务实例
void polaris_instance_destroy(polaris_instance** instance);

/// @brief 获取服务实例ID
///
/// @param instance 服务实例
/// @return const char* 服务实例ID
const char* polaris_instance_get_id(polaris_instance* instance);

/// @brief 获取服务实例HOST
///
/// @param instance 服务实例
/// @return const char* 服务实例HOST
const char* polaris_instance_get_host(polaris_instance* instance);

/// @brief 获取服务实例PORT
///
/// @param instance 服务实例
/// @return int 服务实例PORT
int polaris_instance_get_port(polaris_instance* instance);

/// @brief 获取服务实例vpc id
///
/// @param instance 服务实例
/// @return const char* 服务实例vpc id
const char* polaris_instance_get_vpc_id(polaris_instance* instance);

/// @brief 获取服务实例权重
///
/// @param instance 服务实例
/// @return uint32_t 服务实例权重
uint32_t polaris_instance_get_weight(polaris_instance* instance);

/// @brief 获取服务实例协议
///
/// @param instance 服务实例
/// @return const char* 服务实例协议
const char* polaris_instance_get_protocol(polaris_instance* instance);

/// @brief 获取服务实例版本
///
/// @param instance 服务实例
/// @return const char* 服务实例版本
const char* polaris_instance_get_version(polaris_instance* instance);

/// @brief 获取服务实例优先级
///
/// @param instance 服务实例
/// @return int 服务实例优先级
int polaris_instance_get_priority(polaris_instance* instance);

/// @brief 服务实例是否健康
///
/// @param instance 服务实例
/// @return bool 服务实例是否健康
bool polaris_instance_is_healthy(polaris_instance* instance);

/// @brief 获取服务实例元数据
///
/// @param instance 服务实例
/// @param item_name 元数据名字
/// @return const char* 元数据值
const char* polaris_instance_get_metadata(polaris_instance* instance, const char* item_name);

/// @brief 获取服务实例逻辑SET
///
/// @param instance 服务实例
/// @return const char* 服务实例逻辑SET
const char* polaris_instance_get_logic_set(polaris_instance* instance);

/// @brief 获取服务实例所在Region
///
/// @param instance 服务实例
/// @return const char* 服务实例所在Region
const char* polaris_instance_get_region(polaris_instance* instance);

/// @brief 获取服务实例所在Zone
///
/// @param instance 服务实例
/// @return const char* 服务实例所在Zone
const char* polaris_instance_get_zone(polaris_instance* instance);

/// @brief 获取服务实例所在Campus
///
/// @param instance 服务实例
/// @return const char* 服务实例所在Campus
const char* polaris_instance_get_campus(polaris_instance* instance);

///////////////////////////////////////////////////////////////////////////////

/// @brief 获取服务实例应答
typedef struct _polaris_instances_resp polaris_instances_resp;

/// @brief 销毁获取服务实例应答对象
///
/// @param instances_resp 服务实例应答对象
void polaris_instances_resp_destroy(polaris_instances_resp** instances_resp);

/// @brief 获取服务实例应答包含实例数量
///
/// @param instances_resp 服务实例应答
/// @return int 服务实例应答包含服务实例数量
int polaris_instances_resp_size(polaris_instances_resp* instances_resp);

/// @brief 从服务实例应答中获取服务实例
///
/// @param instances_resp 服务实例应答
/// @param index 服务实例中第几个服务实例
/// @return polaris_instance* 服务实例
polaris_instance* polaris_instances_resp_get_instance(polaris_instances_resp* instances_resp,
                                                      int index);

///////////////////////////////////////////////////////////////////////////////

/// @brief 获取单个服务实例
///
/// @param api POLARIS API对象
/// @param get_one_instance_req 获取单个服务实例请求
/// @param instance 服务实例
/// @return int 返回码，0表示获取成功
int polaris_api_get_one_instance(polaris_api* api,
                                 polaris_get_one_instance_req* get_one_instance_req,
                                 polaris_instance** instance);

/// @brief 获取单个服务实例
///
/// @param api POLARIS API对象
/// @param get_one_instance_req 获取单个服务实例请求
/// @param instances_resp 服务实例应答，成功时里面只包含一个服务实例
/// @return int 返回码，0表示获取成功
int polaris_api_get_one_instance_resp(polaris_api* api,
                                      polaris_get_one_instance_req* get_one_instance_req,
                                      polaris_instances_resp** instances_resp);

/// @brief 批量获取服务实例
///
/// @param api POLARIS API对象
/// @param get_instances_req 批量获取服务实例请求
/// @param instances_resp 服务实例应答
/// @return int 返回码，0表示成功
int polaris_api_get_instances_resp(polaris_api* api, polaris_get_instances_req* get_instances_req,
                                   polaris_instances_resp** instances_resp);

/// @brief 同步获取服务下全部服务实例，返回的实例与控制台看到的一致
///
/// @param api POLARIS API对象
/// @param get_instances_req 批量获取服务实例请求
/// @param instances_resp 服务实例应答
/// @return int 返回码，0表示成功
int polaris_api_get_all_instances(polaris_api* api, polaris_get_instances_req* get_instances_req,
                                  polaris_instances_resp** instances_resp);

///////////////////////////////////////////////////////////////////////////////

/// @brief 服务实例调用结果上报
typedef struct _polaris_service_call_result polaris_service_call_result;

/// @brief 服务实例调用结果
typedef enum {
  POLARIS_CALL_RET_OK      = 0,  ///< 服务实例调用成功
  POLARIS_CALL_RET_TIMEOUT = 1,  ///< 服务实例调用超时，会用于熔断
  POLARIS_CALL_RET_ERROR   = 2   ///< 服务实例调用出错，会用于熔断
} polaris_call_ret_status;

/// @brief 创建服务实例调用结果上报对象
///
/// @param service_namespace 命名空间
/// @param service_name 服务名
/// @param instance_id 服务实例ID
/// @return polaris_service_call_result* 服务实例调用结果上报对象
polaris_service_call_result* polaris_service_call_result_new(const char* service_namespace,
                                                             const char* service_name,
                                                             const char* instance_id);

/// @brief 释放服务实例调用结果上报对象
///
/// @param service_call_result 服务实例调用结果上报对象
void polaris_service_call_result_destroy(polaris_service_call_result** service_call_result);

/// @brief 设置服务实例调用结果
///
/// @param service_call_result 服务实例调用结果上报对象
/// @param call_ret_status 服务实例调用结果
void polaris_service_call_result_set_ret_status(polaris_service_call_result* service_call_result,
                                                polaris_call_ret_status call_ret_status);

/// @brief 设置服务实例调用返回码，业务返回码目前只用于上报监控系统
///
/// @param service_call_result 服务实例调用结果上报对象
/// @param call_return_code 业务自定义返回码
void polaris_service_call_result_set_ret_code(polaris_service_call_result* service_call_result,
                                              int call_ret_code);

/// @brief 设置服务实例调用延迟
///
/// @param service_call_result 服务实例调用结果上报对象
/// @param delay 服务实例调用延迟，时间单位业务自定义，建议使用ms
void polaris_service_call_result_set_delay(polaris_service_call_result* service_call_result,
                                           uint64_t delay);

/// @brief 服务实例调用结果上报
///
/// @param api POLARIS API对象
/// @param service_call_result 服务实例调用结果上报对象
int polaris_api_update_service_call_result(polaris_api* api,
                                           polaris_service_call_result* service_call_result);

///////////////////////////////////////////////////////////////////////////////

// 服务实例注册请求
typedef struct _polaris_register_instance_req polaris_register_instance_req;

/// @brief 创建服务实例注册请求
///
/// @param service_namespace 服务的命名空间
/// @param service_name 服务的服务名
/// @param service_token 服务的token
/// @param host 服务实例host
/// @param port 服务实例port
/// @return polaris_register_instance_req* 服务实例注册请求
polaris_register_instance_req* polaris_register_instance_req_new(const char* service_namespace,
                                                                 const char* service_name,
                                                                 const char* service_token,
                                                                 const char* host, int port);

/// @brief 释放服务实例注册请求
///
/// @param register_req // 服务实例注册请求
void polaris_register_instance_req_destroy(polaris_register_instance_req** register_req);

/// @brief 设置服务实例注册请求的服务实例vpc id。可选，不设置默认为空
///
/// @param register_req 服务实例注册请求
/// @param vpc_id 服务实例所在vpc id
void polaris_register_instance_req_set_vpc_id(polaris_register_instance_req* register_req,
                                              const char* vpc_id);

/// @brief 设置服务实例注册请求的服务实例协议。可选，不设置默认为空
///
/// @param register_req 服务实例注册请求
/// @param protocol 服务实例协议
void polaris_register_instance_req_set_protocol(polaris_register_instance_req* register_req,
                                                const char* protocol);

/// @brief 设置服务实例注册请求的服务实例权重。可选，不设置默认为100
///
/// @param register_req 服务实例注册请求
/// @param weight 服务实例权重
void polaris_register_instance_req_set_weight(polaris_register_instance_req* register_req,
                                              int weight);

/// @brief 设置服务实例注册请求的服务实例优先级。可选，不设置默认为0
///
/// @param register_req 服务实例注册请求
/// @param priority 服务实例优先级
void polaris_register_instance_req_set_priority(polaris_register_instance_req* register_req,
                                                int priority);

/// @brief 设置服务实例注册请求的服务实例版本。可选，不设置默认为空
///
/// @param register_req 服务实例注册请求
/// @param version 服务实例版本
void polaris_register_instance_req_set_version(polaris_register_instance_req* register_req,
                                               const char* version);

/// @brief 添加服务实例注册请求的服务实例metadata。可选，不设置默认为空
///
/// @param register_req 服务实例注册请求
/// @param key 服务实例metadata的key
/// @param value 服务实例metadata的value
void polaris_register_instance_req_add_metadata(polaris_register_instance_req* register_req,
                                                const char* key, const char* value);

/// @brief 设置服务实例注册请求的服务实例是否开启健康检查。可选，默认不开启
///
/// 如果配置了开启健康检查，则需要以ttl间隔调用心跳上报接口维持心跳。
/// 否则服务端将置该服务实例为不健康
/// @param register_req 服务实例注册请求
/// @param health_check_flag 是否开启健康检查
void polaris_register_instance_req_set_health_check_flag(
    polaris_register_instance_req* register_req, bool health_check_flag);

/// @brief 设置服务实例注册请求的服务实例健康检查的ttl。可选，不填默认为5s
///
/// @param register_req 服务实例注册请求
/// @param ttl 健康检查ttl
void polaris_register_instance_req_set_health_check_ttl(polaris_register_instance_req* register_req,
                                                        int ttl);

/// @brief 设置请求的超时时间。可选，不设置默认为1s
///
/// @param register_req 服务实例注册请求
/// @param timeout 超时时间
void polaris_register_instance_req_set_timeout(polaris_register_instance_req* register_req,
                                               uint64_t timeout);

/// @brief 注册服务实例
///
/// @param api api对象
/// @param req 服务实例注册请求
/// @return int 服务实例注册结果，0表示注册成功，1200表示服务实例已注册
int polaris_api_register_instance(polaris_api* api, polaris_register_instance_req* req);

///////////////////////////////////////////////////////////////////////////////

// 服务实例反注册请求
typedef struct _polaris_deregister_instance_req polaris_deregister_instance_req;

/// @brief 创建服务实例反注册请求
///
/// @param service_namespace 服务的命名空间
/// @param service_name 服务的服务名
/// @param service_token 服务的token
/// @param host 服务实例host
/// @param port 服务实例port
/// @return polaris_deregister_instance_req* 服务实例反注册请求
polaris_deregister_instance_req* polaris_deregister_instance_req_new(const char* service_namespace,
                                                                     const char* service_name,
                                                                     const char* service_token,
                                                                     const char* host, int port);

/// @brief 销毁服务实例反注册请求
///
/// @param deregister_req 服务实例反注册请求
void polaris_deregister_instance_req_destroy(polaris_deregister_instance_req** deregister_req);

/// @brief 设置服务实例注册请求的服务实例vpc id。可选，不设置默认为空
///
/// @param deregister_req 服务实例反注册请求
/// @param vpc_id 服务实例所在vpc id
void polaris_deregister_instance_req_set_vpc_id(polaris_deregister_instance_req* deregister_req,
                                                const char* vpc_id);

/// @brief 设置请求的超时时间。可选，不设置默认为1s
///
/// @param deregister_req 服务实例反注册请求
/// @param timeout 超时时间
void polaris_deregister_instance_req_set_timeout(polaris_deregister_instance_req* deregister_req,
                                                 uint64_t timeout);

/// @brief 服务实例反注册
///
/// @param api api对象
/// @param deregister_req 服务实例反注册请求
/// @return int 服务实例反注册结果，0表示成功
int polaris_api_deregister_instance(polaris_api* api,
                                    polaris_deregister_instance_req* deregister_req);

///////////////////////////////////////////////////////////////////////////////

// 服务实例心跳上报请求
typedef struct _polaris_instance_heartbeat_req polaris_instance_heartbeat_req;

/// @brief 创建服务实例心跳上报请求
///
/// @param service_namespace 服务的命名空间
/// @param service_name 服务的服务名
/// @param service_token 服务的token
/// @param host 服务实例host
/// @param port 服务实例port
/// @return polaris_instance_heartbeat_req* 服务实例反注册请求
polaris_instance_heartbeat_req* polaris_instance_heartbeat_req_new(const char* service_namespace,
                                                                   const char* service_name,
                                                                   const char* service_token,
                                                                   const char* host, int port);

/// @brief 销毁服务实例心跳上报请求
///
/// @param heartbeat_req 服务实例心跳上报请求
void polaris_instance_heartbeat_req_destroy(polaris_instance_heartbeat_req** heartbeat_req);

/// @brief 设置服务实例心跳上报请求的服务实例vpc id。可选，不设置默认为空
///
/// @param heartbeat_req 服务实例心跳上报请求
/// @param vpc_id 服务实例所在vpc id
void polaris_instance_heartbeat_req_set_vpc_id(polaris_instance_heartbeat_req* heartbeat_req,
                                               const char* vpc_id);

/// @brief 设置请求的超时时间。可选，不设置默认为1s
///
/// @param heartbeat_req 服务实例心跳上报请求
/// @param timeout 超时时间
void polaris_instance_heartbeat_req_set_timeout(polaris_instance_heartbeat_req* heartbeat_req,
                                                uint64_t timeout);

/// @brief 服务实例心跳上报请求
///
/// @param api api对象
/// @param heartbeat_req  服务实例心跳上报请求
/// @return int 服务实例心跳上报结果，0表示成功
int polaris_api_instance_heartbeat(polaris_api* api, polaris_instance_heartbeat_req* heartbeat_req);

#ifdef __cplusplus
}
#endif

#endif  // POLARIS_CPP_INCLUDE_POLARIS_POLARIS_API_H_
