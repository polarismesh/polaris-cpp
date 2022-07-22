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

#ifndef POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_QUOTA_ADJUSTER_H_
#define POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_QUOTA_ADJUSTER_H_

#include "polaris/defs.h"
#include "polaris/limit.h"

namespace v1 {
class RateLimitRecord;
}

namespace polaris {

class MetricConnector;
class RateLimitRule;
class Reactor;
class RateLimitWindow;
class RemoteAwareBucket;

enum QuotaAdjusterType {
  kQuotaAdjusterClimb,
};

// 配额调整基类
class QuotaAdjuster : public ServiceBase {
 public:
  QuotaAdjuster(Reactor& reactor, MetricConnector* connector, RemoteAwareBucket* remote_bucket);

  virtual ~QuotaAdjuster();

  // 通过rule初始化
  virtual ReturnCode Init(RateLimitRule* rule) = 0;

  // 记录调用结果
  virtual void RecordResult(const LimitCallResult::Impl& request) = 0;

  virtual void MakeDeleted() = 0;

  // 收集配额变更状态
  virtual void CollectRecord(v1::RateLimitRecord& rate_limit_record) = 0;

  // 根据类型创建对应的配额调整对象
  static QuotaAdjuster* Create(QuotaAdjusterType adjuster_type, RateLimitWindow* window);

 protected:
  Reactor& reactor_;
  MetricConnector* connector_;
  RemoteAwareBucket* remote_bucket_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_QUOTA_ADJUSTER_H_
