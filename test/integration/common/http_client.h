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

#ifndef POLARIS_CPP_TEST_INTEGRATION_COMMON_HTTP_CLIENT_H_
#define POLARIS_CPP_TEST_INTEGRATION_COMMON_HTTP_CLIENT_H_

#include <string>

namespace polaris {

static const char HTTP_GET[]    = "GET";
static const char HTTP_PUT[]    = "PUT";
static const char HTTP_POST[]   = "POST";
static const char HTTP_DELETE[] = "DELETE";

class HttpClient {
public:
  /// @brief 发送HTTP请求
  ///
  /// @param method GET/PUT/DELETE
  /// @param path 请求路径
  /// @param body json格式请求
  /// @param timeout 超时时间
  /// @param response json格式应答
  /// @return int HTTP返回码, 200为应答成功
  static int DoRequest(const std::string& method, const std::string& path, const std::string& body,
                       int timeout, std::string& response);

private:
  static bool ToResponse(const std::string& data, int& code, std::string& body);
};

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_INTEGRATION_COMMON_HTTP_CLIENT_H_