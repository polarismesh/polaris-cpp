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

#ifndef POLARIS_CPP_INCLUDE_POLARIS_LIMIT_H_
#define POLARIS_CPP_INCLUDE_POLARIS_LIMIT_H_

#include <map>
#include <set>
#include <string>

#include "polaris/context.h"
#include "polaris/noncopyable.h"

namespace polaris {

class QuotaRequestImpl;
/// @brief 限流配额请求
class QuotaRequest {
public:
  /// @brief 构造配额限流请求
  QuotaRequest();

  /// @brief 析构限流配额请求
  ~QuotaRequest();

  /// @brief 设置服务命名空间
  void SetServiceNamespace(const std::string& service_namespace);

  /// @brief 设置服务名
  void SetServiceName(const std::string& service_name);

  /// @brief 设置标签用于选择限流配置
  ///
  /// @param labels 标签
  void SetLabels(const std::map<std::string, std::string>& labels);

  /// @brief 设置请求需要分配的配额数量，可选，默认为1
  ///
  /// @param amount 请求的配额数量
  void SetAcquireAmount(int amount);

  /// @brief 设置请求超时时间，可选，单位ms，默认1000ms
  void SetTimeout(uint64_t timeout);

  /// @brief 设置请求的所属服务子集，可选
  ///
  /// @param subset 服务子集
  void SetSubset(const std::map<std::string, std::string>& subset);

private:
  friend class QuotaRequestAccessor;
  QuotaRequestImpl* impl_;
};

/// @brief 配额获取结果
enum QuotaResultCode {
  kQuotaResultOk = 0,   // 配额正常
  kQuotaResultLimited,  // 配额被限流
  kQuotaResultWait      // 需求需要等待重试
};

struct QuotaResultInfo {
  int64_t left_quota_;  // 剩余配额
  int64_t all_quota_;   // 配置的配额
  uint64_t duration_;   // 配置周期
  bool is_degrade_;     // 是否降级
};

class QuotaResponseImpl;
/// @brief 限流配额应答
class QuotaResponse {
public:
  /// @brief 构造配额应答
  explicit QuotaResponse(QuotaResponseImpl* impl);

  /// @brief 析构配额应答
  ~QuotaResponse();

  /// @brief 获取限流配额结果
  ///
  /// @return QuotaResultCode 限流配额结果
  QuotaResultCode GetResultCode() const;

  /// @brief 获取服务实例
  ///
  /// @return const QuotaResultInfo&
  const QuotaResultInfo& GetQuotaResultInfo() const;

  /// @brief 请求需要获取多长时间才能使用配额
  ///
  /// @return uint64_t 等待时间，单位ms
  uint64_t GetWaitTime() const;

private:
  QuotaResponseImpl* impl_;
};

enum LimitCallResultType {
  kLimitCallResultLimited,  // 配额被限制使用
  kLimitCallResultFailed,   // 配额使用失败
  kLimitCallResultOk        // 配额使用成功
};

class LimitCallResultImpl;
/// @brief 配额使用结果
class LimitCallResult {
public:
  /// @brief 构造配额使用结果
  LimitCallResult();

  /// @brief 析构限流配额使用结果
  ~LimitCallResult();

  /// @brief 设置服务命名空间
  void SetServiceNamespace(const std::string& service_namespace);

  /// @brief 设置服务名
  void SetServiceName(const std::string& service_name);

  /// @brief 设置请求的所属服务子集
  ///
  /// @param subset 服务子集
  void SetSubset(const std::map<std::string, std::string>& subset);

  /// @brief 设置标签
  void SetLabels(const std::map<std::string, std::string>& labels);

  /// @brief 设置接口调用结果
  void SetResponseResult(LimitCallResultType result_type);

  /// @brief 设置接口调用时间
  void SetResponseTime(uint64_t response_time);

  /// @brief 设置应答返回码
  void SetResponseCode(int response_code);

private:
  friend class LimitCallResultAccessor;
  LimitCallResultImpl* impl_;
};

class LimitApiImpl;
class LimitApi : Noncopyable {
public:
  ~LimitApi();

  /// @brief 预拉取服务配置的限流规则，默认超时时间1000ms
  ///
  /// @param service_key  需要预拉取限流规则的服务
  /// @param json_rule    json格式的限流规则
  /// @return ReturnCode 拉取结果，拉取失败可重试
  ReturnCode FetchRule(const ServiceKey& service_key, std::string& json_rule);

  /// @brief 预拉取服务配置的限流规则
  ///
  /// @param service_key  需要预拉取限流规则的服务
  /// @param timeout      预拉取请求超时时间，单位为ms
  /// @param json_rule    json格式的限流规则
  /// @return ReturnCode 拉取结果，拉取失败可重试
  ReturnCode FetchRule(const ServiceKey& service_key, uint64_t timeout, std::string& json_rule);

  /// @brief 拉取服务配置的限流规则配置的所有key
  ///
  /// @param service_key  需要预拉取限流规则的服务
  /// @param timeout      预拉取请求超时时间，单位为ms
  /// @param label_keys   限流规则里配置的label的所有key
  /// @return ReturnCode  拉取结果，拉取失败可重试
  ReturnCode FetchRuleLabelKeys(const ServiceKey& service_key, uint64_t timeout,
                                const std::set<std::string>*& label_keys);

  /// @brief 获取配额应答
  ///
  /// @param quota_request  获取配额请求
  /// @param quota_response 获取配额应答
  /// @return ReturnCode    获取返回码
  ReturnCode GetQuota(const QuotaRequest& quota_request, QuotaResponse*& quota_response);

  /// @brief 获取配额
  ///
  /// @param quota_request  获取配额请求
  /// @param quota_result   获取配额结果
  /// @return ReturnCode    获取返回码
  ReturnCode GetQuota(const QuotaRequest& quota_request, QuotaResultCode& quota_result);

  /// @brief 获取配额
  ///
  /// @param quota_request  获取配额请求
  /// @param quota_result   获取配额结果
  /// @param quota_info     当前配额信息
  /// @return ReturnCode    获取返回码
  ReturnCode GetQuota(const QuotaRequest& quota_request, QuotaResultCode& quota_result,
                      QuotaResultInfo& quota_info);

  /// @brief 获取配额
  ///
  /// @param quota_request  获取配额请求
  /// @param quota_result   获取配额结果
  /// @param wait_time      等待时间，单位ms，请求需要排队时间，匀速排队时使用
  /// @return ReturnCode    获取返回码
  ReturnCode GetQuota(const QuotaRequest& quota_request, QuotaResultCode& quota_result,
                      uint64_t& wait_time);

  /// @brief 更新请求配额调用结果
  ///
  /// @param call_result  配额调用结果
  /// @return ReturnCode  返回码
  ReturnCode UpdateCallResult(const LimitCallResult& call_result);

  /// @brief 初始化配额窗口，可选调用，用于提前初始化配窗口减小首次配额延迟
  ///
  /// @param quota_request  配额请求
  /// @return ReturnCode    获取返回码
  ReturnCode InitQuotaWindow(const QuotaRequest& quota_request);

  /// @brief 通过Context创建Limit API对象
  ///
  /// @param Context      SDK上下文对象
  /// @return LimitApi*   创建失败返回NULL
  static LimitApi* Create(Context* Context);

  /// @brief 通过Context创建Limit API对象
  ///
  /// @param Context      SDK上下文对象
  /// @param err_msg      创建失败时的错误原因
  /// @return LimitApi*   创建失败返回NULL
  static LimitApi* Create(Context* Context, std::string& err_msg);

  /// @brief 通过配置创建Limit API对象
  ///
  /// @param config       配置对象
  /// @return LimitApi*   创建失败则返回NULL
  static LimitApi* CreateFromConfig(Config* config);

  /// @brief 通过配置创建Limit API对象
  ///
  /// @param config       配置对象
  /// @param err_msg      创建失败时的错误原因
  /// @return LimitApi*   创建失败返回NULL
  static LimitApi* CreateFromConfig(Config* config, std::string& err_msg);

  /// @brief 通过配置文件创建Limit API对象
  ///
  /// @param file         配置文件
  /// @return LimitApi*   创建失败返回NULL
  static LimitApi* CreateFromFile(const std::string& file);

  /// @brief 通过配置文件创建Limit API对象
  ///
  /// @param file         配置文件
  /// @param err_msg      创建失败时的错误原因
  /// @return LimitApi*   创建失败返回NULL
  static LimitApi* CreateFromFile(const std::string& file, std::string& err_msg);

  /// @brief 通过配置字符串创建Limit API对象
  ///
  /// @param content      配置字符串
  /// @return LimitApi*   创建失败返回NULL
  static LimitApi* CreateFromString(const std::string& content);

  /// @brief 通过配置字符串创建Limit API对象
  ///
  /// @param content      配置字符串
  /// @param err_msg      创建失败时的错误原因
  /// @return LimitApi*   创建失败返回NULL
  static LimitApi* CreateFromString(const std::string& content, std::string& err_msg);

  /// @brief 从默认文件创建配置对象，默认文件为./polaris.yaml，文件不存在则使用默认配置
  ///
  /// @return LimitApi*   创建失败返回NULL
  static LimitApi* CreateWithDefaultFile();

  /// @brief 从默认文件创建配置对象，默认文件为./polaris.yaml，文件不存在则使用默认配置
  ///
  /// @param err_msg      创建失败时的错误原因
  /// @return LimitApi*   创建失败返回NULL
  static LimitApi* CreateWithDefaultFile(std::string& err_msg);

private:
  explicit LimitApi(LimitApiImpl* impl);

  LimitApiImpl* impl_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_LIMIT_H_
