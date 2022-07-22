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

#ifndef POLARIS_CPP_POLARIS_MONITOR_API_STAT_H_
#define POLARIS_CPP_POLARIS_MONITOR_API_STAT_H_

#include "polaris/defs.h"

namespace polaris {

enum ApiStatKey {
  kApiStatConsumerGetOne,
  kApiStatConsumerInitService,
  kApiStatConsumerGetBatch,
  kApiStatConsumerAsyncGetOne,
  kApiStatConsumerAsyncGetBatch,
  kApiStatConsumerGetAll,
  kApiStatConsumerCallResult,
  kApiStatProvderRegsiter,
  kApiStatProviderDeregisger,
  kApiStatProviderHeartbeat,
  kApiStatLimitGetQuota,
  kApiStatLimitUpdateCallResult,
  kApiStatProviderAsyncHeartbeat,

  kApiStatKeyCount,  // NOTICE!! Always be the last one!!!
};

class ApiStatRegistry;
class ContextImpl;

#define RECORD_THEN_RETURN(ret_code) \
  api_stat.Record(ret_code);         \
  return ret_code;

class ApiStat {
 public:
  ApiStat(ContextImpl* context, ApiStatKey stat_key);

  ~ApiStat() { Record(kReturnOk); }  // 析构，如果未调用Record，则默认记录为成功

  void Record(ReturnCode ret_code);  // 主调记录调用结果

 private:
  ApiStatRegistry* registry_;  // API统计中心
  uint64_t api_time_;          // API统计开始时间
  ApiStatKey stat_key_;        // API统计key
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MONITOR_API_STAT_H_
