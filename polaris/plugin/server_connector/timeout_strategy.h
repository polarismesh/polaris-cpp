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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_SERVER_CONNECTOR_TIMEOUT_STRATEGY_H_
#define POLARIS_CPP_POLARIS_PLUGIN_SERVER_CONNECTOR_TIMEOUT_STRATEGY_H_

#include <stdint.h>

namespace polaris {

/// @brief 超时时间策略，用于选择合适的连接超时和请求超时时间
///
/// timeout 默认为 min_timeout，每次失败以 expand 速率扩大，直到成功或最大值
/// 成功后以后 耗时*expand 作为后续超时时间
class TimeoutStrategy {
public:
  TimeoutStrategy() : min_timeout_(0), timeout_(0), max_timeout_(0), expand_(0) {}

  void Init(uint64_t min_timeout, uint64_t max_timeout, float expand);

  // 当前超时时间
  uint64_t GetTimeout() const { return timeout_; }

  // 失败时设置下一次超时时间
  void SetNextRetryTimeout();

  // 成功是设置以后的超时时间
  void SetNormalTimeout(uint64_t time_used);

private:
  uint64_t min_timeout_;  // 最小超时时间
  uint64_t timeout_;      // 当前超时时间
  uint64_t max_timeout_;  // 最大超时时间
  float expand_;          // 扩大乘数
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_SERVER_CONNECTOR_TIMEOUT_STRATEGY_H_
