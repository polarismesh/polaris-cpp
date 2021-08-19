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

#include "api_stat_registry.h"

#include <google/protobuf/wrappers.pb.h>
#include <stddef.h>
#include <v1/request.pb.h>

#include <algorithm>
#include <sstream>
#include <string>

#include "context_internal.h"
#include "logger.h"
#include "model/return_code.h"
#include "monitor/api_stat.h"
#include "polaris/context.h"
#include "utils/static_assert.h"
#include "utils/string_utils.h"
#include "utils/utils.h"

namespace polaris {

extern const char* g_sdk_type;
extern const char* g_sdk_version;

static const char* g_ApiStatKeyMap[] = {"Consumer::InitService",
                                        "Consumer::GetOneInstance",
                                        "Consumer::GetInstances",
                                        "Consumer::AsyncGetOneInstance",
                                        "Consumer::AsyncGetInstances",
                                        "Consumer::GetAllInstances",
                                        "Consumer::UpdateCallResult",
                                        "Provider::Register",
                                        "Provider::Deregister",
                                        "Provider::Heartbeat",
                                        "Limit::GetQuota",
                                        "Limit::UpdateCallResult",
                                        "Provider::AsyncHeartbeat"};

// 静态断言两处stat key的长度相等
STATIC_ASSERT(sizeof(g_ApiStatKeyMap) / sizeof(const char*) == kApiStatKeyCount, "");

static const char* g_DelayRangeStr[] = {
    "[0ms,2ms)",     "[2ms, 10ms)",   "[10ms,50ms)", "[50ms,100ms)",
    "[100ms,150ms)", "[150ms,200ms)", "[200ms,)",
};

static const int kDelayBucketCount = sizeof(g_DelayRangeStr) / sizeof(const char*);

ApiStatRegistry::ApiStatRegistry(Context* context) {
  context_ = context;
  GetAllRetrunCodeInfo(ret_code_info_, success_code_index_);
  ret_code_count_ = ret_code_info_.size();
  api_metrics_    = new int**[kApiStatKeyCount];
  for (int i = 0; i < kApiStatKeyCount; i++) {
    api_metrics_[i] = new int*[ret_code_count_];
    for (int j = 0; j < ret_code_count_; j++) {
      api_metrics_[i][j] = new int[kDelayBucketCount];
      for (int k = 0; k < kDelayBucketCount; k++) {
        api_metrics_[i][j][k] = 0;
      }
    }
  }
}

ApiStatRegistry::~ApiStatRegistry() {
  context_ = NULL;
  for (int i = 0; i < kApiStatKeyCount; i++) {
    for (int j = 0; j < ret_code_count_; j++) {
      delete[] api_metrics_[i][j];
    }
    delete[] api_metrics_[i];
  }
  delete[] api_metrics_;
}

void ApiStatRegistry::Record(ApiStatKey stat_key, ReturnCode ret_code, uint64_t delay) {
  std::size_t ret_code_index = ReturnCodeToIndex(ret_code);
  int delay_index;
  if (delay < 2) {
    delay_index = 0;
  } else if (delay < 10) {
    delay_index = 1;
  } else if (delay < 50) {
    delay_index = 2;
  } else {
    delay_index = (delay / 50) + 2;
    if (delay_index >= kDelayBucketCount) {
      delay_index = kDelayBucketCount - 1;
    }
  }
  ATOMIC_INC(&api_metrics_[stat_key][ret_code_index][delay_index]);
}

void ApiStatLog(google::protobuf::RepeatedField<v1::SDKAPIStatistics>& statistics) {
  for (int i = 0; i < statistics.size(); ++i) {
    v1::SDKAPIStatistics& item = statistics[i];
    std::ostringstream output;
    const v1::SDKAPIStatisticsKey& stat_key = item.key();
    const v1::Indicator& stat_value         = item.value();
    output << "id:" << item.id().value() << ", client_host:" << stat_key.client_host().value()
           << ", api:" << stat_key.sdk_api().value() << ", ret_code:" << stat_key.res_code().value()
           << ", success:" << stat_key.success().value()
           << ", delay_range:" << stat_key.delay_range().value()
           << ", client_version:" << stat_key.client_version().value()
           << ", client_type: " << stat_key.client_type().value()
           << ", result_type: " << v1::APIResultType_Name(stat_key.result())
           << ", uid: " << stat_key.uid()
           << ", value:" << stat_value.total_request_per_minute().value();
    POLARIS_STAT_LOG(LOG_INFO, "sdk api stat %s", output.str().c_str());
  }
}

void ApiStatRegistry::GetApiStatistics(
    google::protobuf::RepeatedField<v1::SDKAPIStatistics>& statistics) {
  const std::string& tontext_uid = context_->GetContextImpl()->GetSdkToken().uid();
  for (int i = 0; i < kApiStatKeyCount; i++) {
    for (int j = 0; j < ret_code_count_; j++) {
      for (int k = 0; k < kDelayBucketCount; k++) {
        if (api_metrics_[i][j][k] == 0) {
          continue;
        }
        v1::SDKAPIStatistics* api_stat = statistics.Add();
        api_stat->mutable_id()->set_value(StringUtils::TypeToStr<uint64_t>(Utils::GetNextSeqId()));
        v1::SDKAPIStatisticsKey* stat_key = api_stat->mutable_key();
        stat_key->mutable_client_host()->set_value(context_->GetContextImpl()->GetApiBindIp());
        stat_key->mutable_sdk_api()->set_value(g_ApiStatKeyMap[i]);
        stat_key->mutable_res_code()->set_value(ret_code_info_[j]->str_code_);
        stat_key->mutable_success()->set_value(j == success_code_index_);
        stat_key->mutable_delay_range()->set_value(g_DelayRangeStr[k]);
        stat_key->mutable_client_version()->set_value(g_sdk_version);
        stat_key->mutable_client_type()->set_value(g_sdk_type);
        stat_key->set_result(static_cast<v1::APIResultType>(ret_code_info_[j]->type_));
        stat_key->set_uid(tontext_uid);
        int count = api_metrics_[i][j][k];
        api_stat->mutable_value()->mutable_total_request_per_minute()->set_value(count);
        ATOMIC_SUB(&api_metrics_[i][j][k], count);  // 扣除
      }
    }
  }
  if (statistics.empty()) {
    POLARIS_STAT_LOG(LOG_INFO, "no sdk api stat this period");
  } else {
    ApiStatLog(statistics);
  }
}

}  // namespace polaris
