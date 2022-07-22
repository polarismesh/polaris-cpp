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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_SERVER_CONNECTOR_SERVER_CONNECTOR_H_
#define POLARIS_CPP_POLARIS_PLUGIN_SERVER_CONNECTOR_SERVER_CONNECTOR_H_

#include <functional>
#include <memory>

#include "polaris/defs.h"
#include "polaris/plugin.h"
#include "polaris/provider.h"
#include "v1/response.pb.h"

namespace polaris {

/// @brief 事件处理回调接口
class ServiceEventHandler {
 public:
  /// @brief 析构函数
  virtual ~ServiceEventHandler() {}

  /// @brief 事件处理逻辑
  ///
  /// @param type 事件监听类型
  /// @param service_key 需要监听的服务
  /// @param data 事件的数据，如果是NULL表示未找到该服务数据
  virtual void OnEventUpdate(const ServiceKey& service_key, ServiceDataType data_type, void* data) = 0;

  /// @brief 同步成功事件回调
  virtual void OnEventSync(const ServiceKey& service_key, ServiceDataType data_type) = 0;
};

enum PolarisRequestType {
  kBlockRequestInit,
  kBlockRegisterInstance,
  kBlockDeregisterInstance,
  kPolarisHeartbeat,
  kPolarisReportClient,
};

const char* PolarisRequestTypeStr(PolarisRequestType request_type);

const ServiceKey& GetPolarisService(Context* context, PolarisRequestType request_type);

// 请求回调
using PolarisCallback = std::function<void(ReturnCode, const std::string&, std::unique_ptr<v1::Response>)>;

/// @brief 扩展点接口：对接Server/Agent的代理，封装了网络通信逻辑
///
/// 接口分为两部分：
///     1. 服务事件监听、反监听，用于定时同步服务实例和服务路由
///     2. 服务注册、反注册、心跳上报、Client上报
class ServerConnector : public Plugin {
 public:
  /// @brief 析构函数
  virtual ~ServerConnector() {}

  /// @brief 通过配置进行初始化
  virtual ReturnCode Init(Config* config, Context* context) = 0;

  /// @brief 注册服务事件监听器
  ///
  /// @param service_key 要监听的服务
  /// @param data_type 服务数据类型
  /// @param sync_interval 服务数据同步间隔
  /// @param disk_revision 加载到的可用磁盘缓存文件中的版本
  /// @param handler 事件处理回调
  /// @return ReturnCode 调用返回码
  virtual ReturnCode RegisterEventHandler(const ServiceKey& service_key, ServiceDataType data_type,
                                          uint64_t sync_interval, const std::string& disk_revision,
                                          ServiceEventHandler* handler) = 0;

  /// @brief 反注册事件监听器
  ///
  /// @param type 事件类型
  /// @param service_key 反监听的服务
  /// @return ReturnCode 调用返回码
  virtual ReturnCode DeregisterEventHandler(const ServiceKey& service_key, ServiceDataType data_type) = 0;

  /// @brief 实现具体的注册服务请求
  ///
  /// @param req 服务实例注册请求，已经被校验为合法
  /// @param timeout_ms 超时时间(毫秒)
  /// @param instance_id 注册成功后服务端返回的实例ID
  /// @return int 调用返回码
  virtual ReturnCode RegisterInstance(const InstanceRegisterRequest& req, uint64_t timeout_ms,
                                      std::string& instance_id) = 0;

  /// @brief 发送同步反注册服务
  ///
  /// @param req 反注册请求，已经被校验为合法
  /// @param timeout_ms 超时时间(毫秒)
  /// @return ReturnCode 调用返回码
  virtual ReturnCode DeregisterInstance(const InstanceDeregisterRequest& req, uint64_t timeout_ms) = 0;

  /// @brief 发送心跳上报请求
  ///
  /// @param req 心跳请求，已经被校验为合法
  /// @param timeout_ms 超时时间(毫秒)
  /// @return ReturnCode 调用返回码
  virtual ReturnCode InstanceHeartbeat(const InstanceHeartbeatRequest& req, uint64_t timeout_ms) = 0;

  /// @brief 异步发送心跳上报请求
  ///
  /// @param req 心跳请求，已经被校验为合法
  /// @param timeout_ms 超时时间(毫秒)
  /// @param callback 请求完成时结果回调，由SDK回调完成后释放
  /// @return ReturnCode 调用返回码
  virtual ReturnCode AsyncInstanceHeartbeat(const InstanceHeartbeatRequest& req, uint64_t timeout_ms,
                                            ProviderCallback* callback) = 0;

  /// @brief 发送Client上报请求
  /// @param host client端的ip地址
  /// @param timeout_ms 超时时间(毫秒)
  /// @param location 上报成功后，返回的client端的location
  /// @return ReturnCode 调用返回码
  virtual ReturnCode AsyncReportClient(const std::string& host, uint64_t timeout_ms, PolarisCallback callback) = 0;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_SERVER_CONNECTOR_SERVER_CONNECTOR_H_
