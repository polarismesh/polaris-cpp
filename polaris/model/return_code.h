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

#ifndef POLARIS_CPP_POLARIS_MODEL_RETURN_CODE_H_
#define POLARIS_CPP_POLARIS_MODEL_RETURN_CODE_H_

#include <stdint.h>

#include <iosfwd>
#include <map>
#include <vector>

#include "polaris/defs.h"

namespace polaris {

// 北极星服务器返回的错误码
enum PolarisServerCode {
  kServerCodeReturnOk = 2000,  // 服务器返回正常

  // 以下五个错误码需要触发熔断
  kServerCodeConnectError = 2001,     // 服务器连接超时
  kServerCodeServerError = 2002,      // 服务器返回500错误
  kServerCodeRpcError = 2003,         // RPC调用出错
  kServerCodeRpcTimeout = 2004,       // RPC调用超时
  kServerCodeInvalidResponse = 2005,  // 服务器应答不合法

  kServerCodeInvalidRequest = 2006,  // 服务器返回请求不合法
  kServerCodeUnauthorized = 2007,    // 请求未授权
  kServerCodeRequestLimit = 2008,    // 请求被限流
  kServerCodeCmdbNotFound = 2009,    // 获取CMDB失败
  kServerCodeRemoteClose = 2010,     // 服务器关闭链接
  kServerCodeUnknownError = 2100,    // 未知错误码
};

PolarisServerCode ToPolarisServerCode(uint32_t code);

// API返回码类型
enum ReturnCodeType {
  kReturnCodeTypeUnknow = 0,      // 调用成功
  kReturnCodeTypeSucc = 1,        // 调用成功
  kReturnCodeTypeUserFail = 2,    // 业务错误
  kReturnCodeTypePolarisFail = 3  // 北极星Server或SDK错误
};

struct ReturnCodeInfo {
  ReturnCodeInfo(const char* message, const char* str_code, ReturnCodeType type, std::size_t stat_index)
      : message_(message), str_code_(str_code), type_(type), stat_index_(stat_index) {}
  const char* message_;     // 返回码对应的消息
  const char* str_code_;    // 返回码上报字符串
  ReturnCodeType type_;     // 返回码类型
  std::size_t stat_index_;  // 返回码索引，用于统计时快速查询返回码

  // 获取未知返回码信息定义
  static ReturnCodeInfo& GetUnkownErrorInfo();

  // 获取返回码信息
  static std::map<ReturnCode, ReturnCodeInfo>& GetReturnCodeInfoMap();
};

// 通过返回码查询统计索引
std::size_t ReturnCodeToIndex(ReturnCode ret_code);

// 获取所有的返回码和成功返回码的所有
void GetAllRetrunCodeInfo(std::vector<ReturnCodeInfo*>& return_code_info, int& success_code_index);

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_RETURN_CODE_H_
