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

#include "model/return_code.h"

#include <map>
#include <string>
#include <utility>

#include "logger.h"
#include "polaris/defs.h"
#include "utils/indestructible.h"

namespace polaris {

PolarisServerCode ToPolarisServerCode(uint32_t code) {
  int ret_code = (code / 10000) * 10;
  switch (ret_code) {
    case 200:
      return kServerCodeReturnOk;
    case 500:  // 服务器执行请求失败
      return kServerCodeServerError;
    case 400:  // 请求不合法
      switch (code) {
        case 401000:
          return kServerCodeUnauthorized;
        case 403001:  // fall through
        case 403002:
          return kServerCodeRequestLimit;
        case 404001:
          return kServerCodeCmdbNotFound;
        default:
          return kServerCodeInvalidRequest;
      }
    default:
      return kServerCodeUnknownError;
  }
}

#define SUCC_CODE(CODE, MSG, STR) \
  code_map.insert(std::make_pair(CODE, ReturnCodeInfo(MSG, STR, kReturnCodeTypeSucc, index++)));

#define USER_CODE(CODE, MSG, STR) \
  code_map.insert(std::make_pair(CODE, ReturnCodeInfo(MSG, STR, kReturnCodeTypeUserFail, index++)));

#define POLARIS_CODE(CODE, MSG, STR) \
  code_map.insert(std::make_pair(CODE, ReturnCodeInfo(MSG, STR, kReturnCodeTypePolarisFail, index++)));

std::map<ReturnCode, ReturnCodeInfo> CreateReturnCodeMap(ReturnCodeInfo& UnknownErrorInfo) {
  std::map<ReturnCode, ReturnCodeInfo> code_map;
  std::size_t index = 0;
  SUCC_CODE(kReturnOk, "success", "Success");
  USER_CODE(kReturnInvalidArgument, "invalid argument", "ErrCodeAPIInvalidArgument");
  USER_CODE(kReturnInvalidConfig, "invalid config", "ErrCodeAPIInvalidConfig");
  POLARIS_CODE(kReturnPluginError, "plugin error", "ErrCodePluginError");
  POLARIS_CODE(kReturnTimeout, "request timetout", "ErrCodeAPITimeoutError");
  USER_CODE(kReturnInvalidState, "invalid state", "ErrCodeInvalidStateError");
  POLARIS_CODE(kReturnServerError, "server error", "ErrCodeServerError");
  POLARIS_CODE(kReturnNetworkFailed, "network error", "ErrCodeNetworkError");
  USER_CODE(kReturnInstanceNotFound, "instance not found", "ErrCodeAPIInstanceNotFound");
  USER_CODE(kReturnInvalidRouteRule, "invalid route rule", "ErrCodeInvalidRouteRule");
  USER_CODE(kReturnRouteRuleNotMatch, "route rule not match", "ErrCodeRouteRuleNotMatch");
  USER_CODE(kReturnServiceNotFound, "service not found", "ErrCodeServiceNotFound");
  USER_CODE(kRetrunCallAfterFork, "call after fork, see examples/fork_support/README.md", "ErrCodeCallAfterFork");
  SUCC_CODE(kReturnExistedResource, "resource already existed", "ErrCodeExistedResource");
  USER_CODE(kReturnUnauthorized, "request unauthorized", "ErrCodeUnauthorized");
  USER_CODE(kReturnHealthyCheckDisable, "healthy check disbale", "ErrCodeHealthyCheckDisable");
  USER_CODE(kRetrunRateLimit, "rate limit", "ErrCodeRateLimit");
  USER_CODE(kReturnNotInit, "resource not init", "ErrCodeNotInit");
  POLARIS_CODE(kReturnServerUnknownError, "unknow server error", "ErrCodeServerUnknownError");
  UnknownErrorInfo.stat_index_ = index++;
  return code_map;
}

ReturnCodeInfo CreateUnkownErrorInfo() {
  return ReturnCodeInfo("unknown error", "ErrCodeUnknown", kReturnCodeTypeUnknow, 0);
}

ReturnCodeInfo& ReturnCodeInfo::GetUnkownErrorInfo() {
  static Indestructible<ReturnCodeInfo> kUnknownErrorInfo(CreateUnkownErrorInfo());
  return *kUnknownErrorInfo.Get();
}

std::map<ReturnCode, ReturnCodeInfo>& ReturnCodeInfo::GetReturnCodeInfoMap() {
  static Indestructible<std::map<ReturnCode, ReturnCodeInfo> > kReturnCodeInfoMap(
      CreateReturnCodeMap(GetUnkownErrorInfo()));
  return *kReturnCodeInfoMap.Get();
}

std::string ReturnCodeToMsg(ReturnCode return_code) {
  std::string err_prefix = std::to_string(static_cast<int>(return_code)) + "-";
  std::map<ReturnCode, ReturnCodeInfo>& return_code_map = ReturnCodeInfo::GetReturnCodeInfoMap();
  std::map<ReturnCode, ReturnCodeInfo>::const_iterator it = return_code_map.find(return_code);
  if (it != return_code_map.end()) {
    return err_prefix + it->second.message_;
  } else {
    return err_prefix + ReturnCodeInfo::GetUnkownErrorInfo().message_;
  }
}

std::size_t ReturnCodeToIndex(ReturnCode return_code) {
  std::map<ReturnCode, ReturnCodeInfo>& return_code_map = ReturnCodeInfo::GetReturnCodeInfoMap();
  std::map<ReturnCode, ReturnCodeInfo>::const_iterator it = return_code_map.find(return_code);
  if (it != return_code_map.end()) {
    return it->second.stat_index_;
  } else {
    return ReturnCodeInfo::GetUnkownErrorInfo().stat_index_;
  }
}

void GetAllRetrunCodeInfo(std::vector<ReturnCodeInfo*>& return_code_info, int& success_code_index) {
  std::map<ReturnCode, ReturnCodeInfo>& return_code_map = ReturnCodeInfo::GetReturnCodeInfoMap();
  for (std::map<ReturnCode, ReturnCodeInfo>::iterator it = return_code_map.begin(); it != return_code_map.end(); ++it) {
    if (it->first == kReturnOk) {
      POLARIS_ASSERT(it->second.stat_index_ == 0);
      success_code_index = 0;
    }
    POLARIS_ASSERT(it->second.stat_index_ == return_code_info.size());
    return_code_info.push_back(&it->second);
  }
  POLARIS_ASSERT(ReturnCodeInfo::GetUnkownErrorInfo().stat_index_ == return_code_info.size());
  return_code_info.push_back(&ReturnCodeInfo::GetUnkownErrorInfo());
}

}  // namespace polaris
