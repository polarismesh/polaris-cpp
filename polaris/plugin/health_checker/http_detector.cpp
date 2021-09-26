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

#include "plugin/health_checker/http_detector.h"

#include <stdlib.h>

#include "logger.h"
#include "plugin/health_checker/health_checker.h"
#include "plugin/plugin_manager.h"
#include "polaris/config.h"
#include "polaris/model.h"
#include "utils/netclient.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

namespace polaris {

HttpHealthChecker::HttpHealthChecker() { timeout_ms_ = 0; }

HttpHealthChecker::~HttpHealthChecker() {}

ReturnCode HttpHealthChecker::Init(Config* config, Context* /*context*/) {
  request_path_ = config->GetStringOrDefault(HealthCheckerConfig::kHttpRequestPathKey,
                                             HealthCheckerConfig::kHttpRequestPathDefault);
  if (request_path_.empty() || request_path_[0] != '/') {
    POLARIS_LOG(LOG_ERROR, "health checker[%s] config %s invalid", kPluginHttpHealthChecker,
                HealthCheckerConfig::kHttpRequestPathKey);
    return kReturnInvalidConfig;
  }
  timeout_ms_ = config->GetIntOrDefault(HealthCheckerConfig::kTimeoutKey,
                                        HealthCheckerConfig::kTimeoutDefault);
  return kReturnOk;
}

ReturnCode HttpHealthChecker::DetectInstance(Instance& instance, DetectResult& detect_result) {
  uint64_t start_time_ms    = Time::GetCurrentTimeMs();
  detect_result.detect_type = kPluginHttpHealthChecker;
  if (request_path_.empty()) {
    detect_result.return_code = kReturnInvalidConfig;
    detect_result.elapse      = Time::GetCurrentTimeMs() - start_time_ms;
    return kReturnInvalidConfig;
  }
  std::string host         = instance.GetHost();
  int port                 = instance.GetPort();
  std::string http_request = std::string("GET ") + request_path_ + " HTTP/1.0\r\n\r\n";
  std::string http_response;
  int retcode = NetClient::TcpSendRecv(host, port, timeout_ms_, http_request, &http_response);
  if (retcode < 0) {
    detect_result.return_code = kReturnNetworkFailed;
    detect_result.elapse      = Time::GetCurrentTimeMs() - start_time_ms;
    return kReturnNetworkFailed;
  }
  if (http_response.find("\r\n\r\n") == std::string::npos) {
    detect_result.return_code = kReturnServerError;
    detect_result.elapse      = Time::GetCurrentTimeMs() - start_time_ms;
    return kReturnServerError;
  }
  size_t pos = http_response.find("\r\n");
  if (pos == std::string::npos) {
    detect_result.return_code = kReturnServerError;
    detect_result.elapse      = Time::GetCurrentTimeMs() - start_time_ms;
    return kReturnServerError;
  }
  std::string status_line = StringUtils::StringTrim(http_response.substr(0, pos));
  pos                     = status_line.find(' ');
  if (pos == std::string::npos) {
    detect_result.return_code = kReturnServerError;
    detect_result.elapse      = Time::GetCurrentTimeMs() - start_time_ms;
    return kReturnServerError;
  }
  status_line = StringUtils::StringTrim(status_line.substr(pos));
  pos         = status_line.find(' ');
  if (pos == std::string::npos) {
    detect_result.return_code = kReturnServerError;
    detect_result.elapse      = Time::GetCurrentTimeMs() - start_time_ms;
    return kReturnServerError;
  }
  int status_code = atoi(status_line.substr(0, pos).c_str());
  if (status_code < 100 || status_code >= 400) {
    detect_result.return_code = kReturnServerError;
    detect_result.elapse      = Time::GetCurrentTimeMs() - start_time_ms;
    return kReturnServerError;
  }
  detect_result.return_code = kReturnOk;
  detect_result.elapse      = Time::GetCurrentTimeMs() - start_time_ms;
  return kReturnOk;
}

}  // namespace polaris
