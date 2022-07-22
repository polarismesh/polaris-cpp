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

#ifndef POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_CLIMB_ADJUSTER_H_
#define POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_CLIMB_ADJUSTER_H_

#include <stdint.h>
#include <v1/metric.pb.h>

#include <vector>

#include "metric/metric_connector.h"
#include "network/grpc/client.h"
#include "polaris/defs.h"
#include "quota/adjuster/climb_config.h"
#include "quota/adjuster/quota_adjuster.h"
#include "quota/model/rate_limit_rule.h"

namespace v1 {
class RateLimitRecord;
}

namespace polaris {

class CallMetricData;
class HealthMetricClimb;
class LimitCallResult;
class Reactor;
class RemoteAwareBucket;

class ClimbAdjuster : public QuotaAdjuster {
 public:
  ClimbAdjuster(Reactor& reactor, MetricConnector* connector, RemoteAwareBucket* remote_bucket);

  virtual ~ClimbAdjuster();

  // 初始化，配置未开启调整则返回kReturnInvalidConfig
  virtual ReturnCode Init(RateLimitRule* rule);

  virtual void RecordResult(const LimitCallResult::Impl& request);

  virtual void MakeDeleted();

  virtual void CollectRecord(v1::RateLimitRecord& rate_limit_record);

  // 设置定时上报统计任务和定时调整任务
  static void SetupTimingTask(ClimbAdjuster* adjuster);

  // 定时执行统计数据上报
  static void TimingReport(ClimbAdjuster* adjuster);

  // 执行调整任务
  static void TimingAdjust(ClimbAdjuster* adjuster);

  bool IsDeleted() { return is_deleted_; }

  void InitCallback(ReturnCode ret_code, v1::MetricResponse* response, uint64_t elapsed_time);

  void ReportCallback(ReturnCode ret_code, v1::MetricResponse* response, uint64_t elapsed_time);

  void QueryCallback(ReturnCode ret_code, v1::MetricResponse* response);

 private:
  void SendInitRequest();

  void UpdateLocalTIme(int64_t service_time, uint64_t elapsed_time);

  int64_t GetServerTime();

 private:
  v1::MetricKey metric_key_;
  v1::MetricRequest report_request_;
  bool is_deleted_;

  ClimbMetricConfig metric_config_;
  ClimbTriggerPolicy trigger_policy_;
  ClimbThrottling throttling_;  // 执行调整参数

  int64_t local_time_diff_;  // 本地时间差异，正为本地时间快

  CallMetricData* call_metric_data_;
  HealthMetricClimb* health_metric_climb_;

  std::vector<RateLimitAmount> limit_amounts_;  // 当前配额配置
};

// 初始化或同步应答回调
class MetricResponseCallback : public grpc::RpcCallback<v1::MetricResponse> {
 public:
  MetricResponseCallback(ClimbAdjuster* adjuster_, MetricRpcType rpc_type);

  virtual ~MetricResponseCallback();

  virtual void OnSuccess(v1::MetricResponse* response);

  virtual void OnError(ReturnCode ret_code);

 private:
  ClimbAdjuster* adjuster_;
  MetricRpcType rpc_type_;
  uint64_t begin_time_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_ADJUSTER_CLIMB_ADJUSTER_H_
