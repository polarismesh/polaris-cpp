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

#ifndef POLARIS_CPP_POLARIS_MONITOR_API_STAT_REGISTRY_H_
#define POLARIS_CPP_POLARIS_MONITOR_API_STAT_REGISTRY_H_

#include <stdint.h>
#include <vector>

#include "api_stat.h"
#include "polaris/defs.h"

namespace google {
namespace protobuf {
template <typename T>
class RepeatedField;
}
}  // namespace google
namespace v1 {
class SDKAPIStatistics;
}

namespace polaris {

class Context;
struct ReturnCodeInfo;

class ApiStatRegistry {
public:
  explicit ApiStatRegistry(Context* context);

  ~ApiStatRegistry();

  // 记录API调用结果和延迟
  void Record(ApiStatKey stat_key, ReturnCode ret_code, uint64_t delay);

  void GetApiStatistics(google::protobuf::RepeatedField<v1::SDKAPIStatistics>& statistics);

private:
  Context* context_;
  std::vector<ReturnCodeInfo*> ret_code_info_;
  int ret_code_count_;
  int success_code_index_;
  int*** api_metrics_;  // 三维数组，三个维度分别为：API key, ret_code索引, delay区间
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MONITOR_API_STAT_REGISTRY_H_
