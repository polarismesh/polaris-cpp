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

#include "quota/rate_limit_window.h"

#include <google/protobuf/wrappers.pb.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <v1/ratelimit.pb.h>
#include <v1/request.pb.h>

#include <memory>
#include <utility>

#include "logger.h"
#include "polaris/limit.h"
#include "polaris/log.h"
#include "quota/adjuster/quota_adjuster.h"
#include "quota/quota_bucket_qps.h"
#include "quota/quota_model.h"
#include "quota/rate_limit_connector.h"
#include "quota/service_rate_limiter.h"
#include "reactor/reactor.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

namespace polaris {

class MetricConnector;

RateLimitWindow::RateLimitWindow(Reactor& reactor, MetricConnector* metric_connector,
                                 const RateLimitWindowKey& key)
    : reactor_(reactor), metric_connector_(metric_connector), rule_(NULL),
      service_rate_limit_data_(NULL), cache_key_(key), allocating_bucket_(NULL),
      traffic_shaping_bucket_(NULL), last_use_time_(Time::GetCurrentTimeMs()), expire_time_(0),
      is_deleted_(false), quota_adjuster_(NULL), usage_info_(NULL) {}

RateLimitWindow::~RateLimitWindow() {
  rule_ = NULL;
  if (service_rate_limit_data_ != NULL) {
    service_rate_limit_data_->DecrementRef();
    service_rate_limit_data_ = NULL;
  }
  if (allocating_bucket_ != NULL) {
    delete allocating_bucket_;
    allocating_bucket_ = NULL;
  }
  if (traffic_shaping_bucket_ != NULL) {
    delete traffic_shaping_bucket_;
    traffic_shaping_bucket_ = NULL;
  }
  if (quota_adjuster_ != NULL) {
    quota_adjuster_->MakeDeleted();
    quota_adjuster_ = NULL;
  }
  for (std::map<uint64_t, LimitRecordCount>::iterator it = limit_record_count_.begin();
       it != limit_record_count_.end(); ++it) {
    delete it->second.pass_count_;
    delete it->second.limit_count_;
  }
  if (usage_info_ != NULL) {
    delete usage_info_;
    usage_info_ = NULL;
  }
}

ReturnCode RateLimitWindow::Init(ServiceData* service_rate_limit_data, RateLimitRule* rule,
                                 const std::string& metric_id, RateLimitConnector* connector) {
  static const uint64_t kExpireFactor = 3;  // 淘汰因子，过期时间=MaxDuration * ExpireFactor
  static const uint64_t kMinExpireDuration = 60 * 1000;  // 最短淘汰时间，1min

  service_rate_limit_data_ = service_rate_limit_data;
  rule_                    = rule;
  metric_id_               = metric_id;

  // 初始化相关参数
  expire_time_ = rule_->GetMaxValidDuration() * kExpireFactor;
  if (expire_time_ < kMinExpireDuration) {
    expire_time_ = kMinExpireDuration;
  }

  // 初始化流量整型窗口
  ServiceRateLimiter* service_rate_limiter = ServiceRateLimiter::Create(rule_->GetActionType());
  if (service_rate_limiter == NULL) {
    return kReturnInvalidState;
  }
  ReturnCode ret_code = service_rate_limiter->InitQuotaBucket(rule_, traffic_shaping_bucket_);
  delete service_rate_limiter;
  if (ret_code != kReturnOk) {
    return ret_code;
  }

  // 初始化配额窗口
  if (rule_->GetResourceType() == v1::Rule::QPS) {
    allocating_bucket_ = new RemoteAwareQpsBucket(rule_);
  } else {  // 暂时不支持其他类型
    POLARIS_ASSERT(false);
  }
  const std::vector<RateLimitAmount>& amounts = rule->GetRateLimitAmount();
  for (std::size_t i = 0; i < amounts.size(); ++i) {
    LimitRecordCount& record_count = limit_record_count_[amounts[i].valid_duration_];
    record_count.max_amount_       = 0;
    record_count.pass_count_       = new sync::Atomic<uint32_t>();
    record_count.limit_count_      = new sync::Atomic<uint32_t>();
  }
  quota_adjuster_ = QuotaAdjuster::Create(kQuotaAdjusterClimb, this);
  if (rule_->GetRateLimitType() == v1::Rule::LOCAL) {  // 本地模式不需要与Server通信直接
    init_notify_.NotifyAll();
    return kReturnOk;
  }

  // TODO 目前不需要同步等待初始化，可改为通过控制台配置
  init_notify_.NotifyAll();
  // 全局模式提交立即初始化任务
  reactor_.SubmitTask(new WindowSyncTask(this, connector));
  return kReturnOk;
}

ReturnCode RateLimitWindow::WaitRemoteInit(uint64_t timeout) {
  if (init_notify_.IsNotified()) {
    return kReturnOk;
  }
  // 使用条件变量等待
  init_notify_.Wait(timeout);
  return init_notify_.IsNotified() ? kReturnOk : kReturnTimeout;
}

bool RateLimitWindow::CheckRateLimitRuleRevision(const std::string& rule_revision) {
  return !is_deleted_ && rule_->GetRevision() == rule_revision;
}

QuotaResponse* RateLimitWindow::AllocateQuota(int64_t acquire_amount) {
  last_use_time_ = Time::GetCurrentTimeMs();
  QuotaResponse* quota_response;
  QuotaResult* result = traffic_shaping_bucket_->GetQuota(acquire_amount);
  if (result->result_code_ == kQuotaResultLimited) {  // 整形窗口限流
    quota_response = QuotaResponseImpl::CreateResponse(kQuotaResultLimited);
    traffic_shaping_record_++;  // 记录被流量整型限流
  } else {
    LimitAllocateResult limit_result;
    quota_response =
        allocating_bucket_->Allocate(acquire_amount, this->GetServerTime(), &limit_result);
    is_degrade_ = limit_result.is_degrade_;
    if (quota_response->GetResultCode() == kQuotaResultOk) {
      for (std::map<uint64_t, LimitRecordCount>::iterator it = limit_record_count_.begin();
           it != limit_record_count_.end(); ++it) {
        (*it->second.pass_count_) += acquire_amount;  // 记录通过的配额
      }
    } else {
      LimitRecordCount& record_count = limit_record_count_[limit_result.violate_duration_];
      record_count.max_amount_       = limit_result.max_amount_;
      (*record_count.limit_count_) += acquire_amount;  // 记录被配额限制
    }
  }
  delete result;
  return quota_response;
}

void RateLimitWindow::GetInitRequest(metric::v2::RateLimitInitRequest* request) {
  POLARIS_ASSERT(request != NULL);
  metric::v2::LimitTarget* target = request->mutable_target();
  target->set_namespace_(rule_->GetService().namespace_);
  target->set_service(rule_->GetService().name_);
  std::size_t sharp_index = metric_id_.find_last_of("#");
  target->set_labels(metric_id_.substr(sharp_index + 1));
  metric::v2::QuotaMode quota_mode =
      rule_->GetAmountMode() == v1::Rule::GLOBAL_TOTAL ? metric::v2::WHOLE : metric::v2::DIVIDE;
  const std::vector<RateLimitAmount>& amounts = rule_->GetRateLimitAmount();
  for (std::size_t i = 0; i < amounts.size(); ++i) {
    metric::v2::QuotaTotal* total = request->add_totals();
    total->set_maxamount(amounts[i].max_amount_);
    total->set_duration(amounts[i].valid_duration_ / 1000);
    total->set_mode(quota_mode);
  }
}

void RateLimitWindow::OnInitResponse(const metric::v2::RateLimitInitResponse& response,
                                     int64_t time_diff) {
  UpdateServiceTimeDiff(time_diff);
  RemoteQuotaResult result;
  result.local_usage_        = NULL;
  result.curret_server_time_ = this->GetServerTime();
  if (response.timestamp() > 0) {
    result.remote_usage_.create_server_time_ = static_cast<uint64_t>(response.timestamp());
  } else {
    result.remote_usage_.create_server_time_ = result.curret_server_time_;
  }
  for (int i = 0; i < response.counters_size(); ++i) {
    const metric::v2::QuotaCounter& counter                         = response.counters(i);
    uint64_t duration_ms                                            = counter.duration() * 1000ull;
    result.remote_usage_.quota_usage_[duration_ms].quota_allocated_ = counter.left();
    counter_key_duration_[counter.counterkey()]                     = counter.duration();
    duration_counter_key_[counter.duration()]                       = counter.counterkey();
  }
  allocating_bucket_->SetRemoteQuota(result);
}

void RateLimitWindow::GetReprotRequest(metric::v2::RateLimitReportRequest* request) {
  POLARIS_ASSERT(request != NULL);
  uint64_t current_server_time = this->GetServerTime();
  request->set_timestamp(static_cast<int64_t>(current_server_time));
  if (usage_info_ != NULL) {
    delete usage_info_;
  }
  usage_info_ = allocating_bucket_->GetQuotaUsage(current_server_time);
  const std::vector<RateLimitAmount>& amounts = rule_->GetRateLimitAmount();
  for (std::size_t i = 0; i < amounts.size(); ++i) {
    metric::v2::QuotaSum* sum = request->add_quotauses();
    QuotaUsage& quota_usage   = usage_info_->quota_usage_[amounts[i].valid_duration_];
    sum->set_used(quota_usage.quota_allocated_);
    sum->set_limited(quota_usage.quota_rejected_);
    sum->set_counterkey(duration_counter_key_[amounts[i].valid_duration_ / 1000]);
  }
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    POLARIS_LOG(LOG_TRACE, "window report with request: %s", request->ShortDebugString().c_str());
  }
}

uint64_t RateLimitWindow::OnReportResponse(const metric::v2::RateLimitReportResponse& response,
                                           int64_t time_diff) {
  UpdateServiceTimeDiff(time_diff);
  RemoteQuotaResult result;
  result.local_usage_        = usage_info_;
  result.curret_server_time_ = this->GetServerTime();
  if (response.timestamp() > 0) {
    result.remote_usage_.create_server_time_ = static_cast<uint64_t>(response.timestamp());
  } else {
    result.remote_usage_.create_server_time_ = result.curret_server_time_;
  }
  for (int i = 0; i < response.quotalefts_size(); ++i) {
    const metric::v2::QuotaLeft& left = response.quotalefts(i);
    uint32_t duration                 = counter_key_duration_[left.counterkey()];
    result.remote_usage_.quota_usage_[duration * 1000].quota_allocated_ = left.left();
  }
  uint64_t next_report_time = allocating_bucket_->SetRemoteQuota(result);
  if (usage_info_ != NULL) {
    delete usage_info_;
    usage_info_ = NULL;
  }
  uint32_t report_interval = rule_->GetRateLimitReport().IntervalWithJitter();
  return next_report_time < report_interval ? next_report_time : report_interval;
}

bool RateLimitWindow::IsExpired() {
  return last_use_time_ + expire_time_ < Time::GetCurrentTimeMs();
}

void RateLimitWindow::UpdateCallResult(const LimitCallResult& call_result) {
  if (quota_adjuster_ != NULL) {
    quota_adjuster_->RecordResult(call_result);
  }
}

bool RateLimitWindow::CollectRecord(v1::RateLimitRecord& rate_limit_record) {
  uint64_t current_time        = Time::GetCurrentTimeMs();
  uint32_t shaping_limit_count = traffic_shaping_record_.Exchange(0);
  if (shaping_limit_count != 0) {
    v1::LimitStat* limit_stat = rate_limit_record.mutable_limit_stats()->Add();
    limit_stat->set_reason(rule_->GetActionString());
    limit_stat->set_period_times(shaping_limit_count);
    Time::Uint64ToTimestamp(current_time, limit_stat->mutable_time());
  }
  v1::LimitMode limit_mode = v1::GlobalMode;
  if (rule_->GetRateLimitType() == v1::Rule::LOCAL) {
    limit_mode = v1::LocalMode;
  } else if (is_degrade_.Load()) {
    limit_mode = v1::DegradeMode;
  }
  const std::vector<RateLimitAmount>& amounts = rule_->GetRateLimitAmount();
  for (std::size_t i = 0; i < amounts.size(); ++i) {
    LimitRecordCount& record_count = limit_record_count_[amounts[i].valid_duration_];
    uint32_t pass_count            = record_count.pass_count_->Exchange(0);
    uint32_t limit_count           = record_count.limit_count_->Exchange(0);
    if (pass_count != 0 || limit_count != 0) {
      v1::LimitStat* limit_stat = rate_limit_record.mutable_limit_stats()->Add();
      limit_stat->set_reason("amount:" + StringUtils::TypeToStr(record_count.max_amount_) + "/" +
                             StringUtils::TypeToStr(amounts[i].valid_duration_ / 1000) + "s");
      limit_stat->set_pass(pass_count);
      limit_stat->set_period_times(limit_count);
      limit_stat->set_limit_duration(amounts[i].valid_duration_);
      limit_stat->set_mode(limit_mode);
      Time::Uint64ToTimestamp(current_time, limit_stat->mutable_time());
    }
  }
  if (quota_adjuster_ != NULL) {
    quota_adjuster_->CollectRecord(rate_limit_record);
  }
  if (rate_limit_record.limit_stats_size() > 0 || rate_limit_record.threshold_changes_size() > 0) {
    // 有数据需要上报
    rate_limit_record.set_namespace_(rule_->GetService().namespace_);
    rate_limit_record.set_service(rule_->GetService().name_);
    rate_limit_record.set_rule_id(rule_->GetId());
    rate_limit_record.set_rate_limiter(rule_->GetActionString());
    std::size_t label_begin = metric_id_.find_last_of("#");
    if (label_begin != std::string::npos) {
      rate_limit_record.set_labels(metric_id_.substr(label_begin + 1));
      std::size_t subset_begin = metric_id_.find_last_of("#", label_begin - 1);
      if (subset_begin != std::string::npos) {
        subset_begin += 1;
        rate_limit_record.set_subset(metric_id_.substr(subset_begin, label_begin - subset_begin));
      } else {
        rate_limit_record.set_subset(rule_->GetSubsetAsString());
      }
    } else {
      rate_limit_record.set_labels(rule_->GetLabelsAsString());
      rate_limit_record.set_labels(rule_->GetLabelsAsString());
    }
    return true;
  }
  return false;
}

uint64_t RateLimitWindow::GetServerTime() {
  uint64_t current_time = Time::GetCurrentTimeMs();
  int64_t time_diff     = time_diff_.Load();
  if (time_diff >= 0) {
    return current_time + static_cast<uint64_t>(time_diff);
  } else {
    return current_time - static_cast<uint64_t>(-time_diff);
  }
}

void RateLimitWindow::UpdateServiceTimeDiff(int64_t time_diff) { time_diff_ = time_diff; }

void RateLimitWindow::UpdateConnection(const std::string& connection_id) {
  if (connection_id_ != connection_id) {
    connection_id_ = connection_id;
    counter_key_duration_.clear();
    duration_counter_key_.clear();
  }
}

}  // namespace polaris
