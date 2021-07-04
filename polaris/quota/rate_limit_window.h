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

#ifndef POLARIS_CPP_POLARIS_QUOTA_RATE_LIMIT_WINDOW_H_
#define POLARIS_CPP_POLARIS_QUOTA_RATE_LIMIT_WINDOW_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include <v1/request.pb.h>
#include <v2/ratelimit_v2.pb.h>

#include "grpc/grpc_client.h"
#include "polaris/model.h"
#include "quota/model/rate_limit_rule.h"
#include "reactor/task.h"
#include "sync/atomic.h"
#include "sync/cond_var.h"

namespace polaris {

class LimitCallResult;
class MetricConnector;
class QuotaAdjuster;
class QuotaBucket;
class QuotaResponse;
class RateLimitConnector;
class Reactor;

// 配额使用情况
struct QuotaUsage {
  QuotaUsage() : quota_allocated_(0), quota_rejected_(0) {}
  uint64_t quota_allocated_;
  uint64_t quota_rejected_;
};

struct QuotaUsageInfo {
  uint64_t create_server_time_;                 // 配额信息创建时间
  std::map<uint64_t, QuotaUsage> quota_usage_;  // 配额使用详情
};

struct RemoteQuotaResult {
  uint64_t curret_server_time_;  // 当前服务器时间
  QuotaUsageInfo* local_usage_;  // 上报时获取的分配信息用于上报成功后扣除
  QuotaUsageInfo remote_usage_;  // 远程配额汇总结果
};

struct LimitRecordCount {
  uint32_t max_amount_;                  // 配额
  sync::Atomic<uint32_t>* pass_count_;   // 通过次数
  sync::Atomic<uint32_t>* limit_count_;  // 限流次数
};

struct LimitAllocateResult {
  uint32_t max_amount_;
  uint64_t violate_duration_;
  bool is_degrade_;
};

// 远程配额分配令牌桶基类
class QuotaResponse;
class RemoteAwareBucket {
public:
  virtual ~RemoteAwareBucket() {}

  // 执行配额分配操作，返回配额分配结果，并返回是否需要立即上报和限流是违反的配额配置duration
  virtual QuotaResponse* Allocate(int64_t acquire_amount, uint64_t current_server_time,
                                  LimitAllocateResult* limit_result) = 0;

  // 执行配额回收操作
  virtual void Release() = 0;

  // 设置通过限流服务器获取的远程配额
  virtual uint64_t SetRemoteQuota(const RemoteQuotaResult& remote_quota_result) = 0;

  // 获取已经分配的配信息
  virtual QuotaUsageInfo* GetQuotaUsage(uint64_t current_server_time) = 0;

  // 更新动态配额配置，用于配额调整算法动态调整配额
  virtual void UpdateLimitAmount(const std::vector<RateLimitAmount>& amounts) = 0;
};

class RateLimitWindow : public ServiceBase {
public:
  RateLimitWindow(Reactor& reactor, MetricConnector* metric_connector,
                  const RateLimitWindowKey& key);

  ReturnCode Init(ServiceData* service_rate_limit_data, RateLimitRule* rule,
                  const std::string& metric_id, RateLimitConnector* connector);

  ReturnCode WaitRemoteInit(uint64_t timeout);

  bool CheckRateLimitRuleRevision(const std::string& rule_revision);

  QuotaResponse* AllocateQuota(int64_t acquire_amount);

  const std::string& GetMetricId() { return metric_id_; }

  const ServiceKey& GetMetricCluster() { return rule_->GetCluster(); }

  // 初始化返回的应答回调
  void GetInitRequest(metric::v2::RateLimitInitRequest* request);
  void OnInitResponse(const metric::v2::RateLimitInitResponse& response, int64_t time_diff);

  // 上报返回的应答回调
  void GetReprotRequest(metric::v2::RateLimitReportRequest* request);
  uint64_t OnReportResponse(const metric::v2::RateLimitReportResponse& response, int64_t time_diff);

  bool IsExpired();  // 是否过期

  const RateLimitWindowKey& GetCacheKey() { return cache_key_; }

  RateLimitRule* GetRateLimitRule() { return rule_; }

  void MakeDeleted() { is_deleted_ = true; }

  bool IsDeleted() { return is_deleted_; }

  Reactor& GetReactor() { return reactor_; }

  void UpdateCallResult(const LimitCallResult& call_result);

  MetricConnector* GetMetricConnector() { return metric_connector_; }

  RemoteAwareBucket* GetRemoteBucket() { return allocating_bucket_; }

  bool CollectRecord(v1::RateLimitRecord& rate_limit_record);

  uint64_t GetServerTime();

  const std::string& GetConnectionId() const { return connection_id_; }

  // 更新与metric server连接的ID，并清除旧server下发的counter key信息
  void UpdateConnection(const std::string& connection_id);

private:
  virtual ~RateLimitWindow();

  void UpdateServiceTimeDiff(int64_t time_diff);

private:
  Reactor& reactor_;
  MetricConnector* metric_connector_;
  RateLimitRule* rule_;                   // 关联的限流规则
  ServiceData* service_rate_limit_data_;  // 维持服务限流规则索引，保证rule_指向数据不释放
  RateLimitWindowKey cache_key_;
  std::string metric_id_;  // 用于上报的ID

  RemoteAwareBucket* allocating_bucket_;  // 执行流量分配的令牌桶
  QuotaBucket* traffic_shaping_bucket_;   // 流量整型算法桶

  sync::Atomic<int64_t> time_diff_;
  sync::CondVarNotify init_notify_;

  uint64_t last_use_time_;
  uint64_t expire_time_;  // 淘汰周期
  bool is_deleted_;

  QuotaAdjuster* quota_adjuster_;

  sync::Atomic<uint32_t> traffic_shaping_record_;
  sync::Atomic<bool> is_degrade_;
  std::map<uint64_t, LimitRecordCount> limit_record_count_;

  QuotaUsageInfo* usage_info_;
  std::string connection_id_;  // 同步时客户端使用的连接id
  std::map<uint32_t, uint32_t> counter_key_duration_;
  std::map<uint32_t, uint32_t> duration_counter_key_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_QUOTA_RATE_LIMIT_WINDOW_H_
