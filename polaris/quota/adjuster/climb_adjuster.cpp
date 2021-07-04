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

#include "quota/adjuster/climb_adjuster.h"

#include <google/protobuf/wrappers.pb.h>
#include <inttypes.h>
#include <stddef.h>
#include <v1/code.pb.h>
#include <v1/ratelimit.pb.h>

#include <map>
#include <string>
#include <utility>

#include "logger.h"
#include "polaris/log.h"
#include "quota/adjuster/climb_call_metric.h"
#include "quota/adjuster/climb_health_metric.h"
#include "quota/quota_model.h"
#include "quota/rate_limit_window.h"
#include "reactor/reactor.h"
#include "reactor/task.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

namespace v1 {
class RateLimitRecord;
}

namespace polaris {

class LimitCallResult;

ClimbAdjuster::ClimbAdjuster(Reactor& reactor, MetricConnector* connector,
                             RemoteAwareBucket* remote_bucket)
    : QuotaAdjuster(reactor, connector, remote_bucket), is_deleted_(false), local_time_diff_(0),
      call_metric_data_(NULL), health_metric_climb_(NULL) {}

ClimbAdjuster::~ClimbAdjuster() {
  if (call_metric_data_ != NULL) {
    delete call_metric_data_;
    call_metric_data_ = NULL;
  }
  if (health_metric_climb_ != NULL) {
    delete health_metric_climb_;
    health_metric_climb_ = NULL;
  }
}

ReturnCode ClimbAdjuster::Init(RateLimitRule* rule) {
  // 初始化配置
  const v1::ClimbConfig& climb_config = rule->GetAdjuster().climb();
  if (!climb_config.enable().value()) {
    return kReturnInvalidConfig;
  }
  metric_key_.set_namespace_(rule->GetService().namespace_);
  metric_key_.set_service(rule->GetService().name_);
  metric_key_.set_subset(rule->GetId());
  metric_key_.set_labels(rule->GetRevision());
  metric_key_.set_role(v1::MetricKey::Callee);

  metric_config_.InitMetricConfig(climb_config.metric());
  trigger_policy_.InitPolicy(climb_config.policy());
  throttling_.InitClimbThrottling(climb_config.throttling());

  call_metric_data_    = new CallMetricData(metric_config_, trigger_policy_);
  health_metric_climb_ = new HealthMetricClimb(trigger_policy_, throttling_);
  limit_amounts_       = rule->GetRateLimitAmount();  // 当前配额

  // 设置定时任务
  reactor_.SubmitTask(new FuncRefTask<ClimbAdjuster>(SetupTimingTask, this));
  return kReturnOk;
}

void ClimbAdjuster::RecordResult(const LimitCallResult& call_result) {
  LimitCallResultAccessor request(call_result);
  call_metric_data_->Record(request.GetCallResultType(), request.GetResponseTime(),
                            request.GetResponseCode());
}

void ClimbAdjuster::MakeDeleted() {
  is_deleted_ = true;
  this->DecrementRef();
}

void ClimbAdjuster::CollectRecord(v1::RateLimitRecord& rate_limit_record) {
  health_metric_climb_->CollectRecord(rate_limit_record);
}

void ClimbAdjuster::SetupTimingTask(ClimbAdjuster* adjuster) {
  if (!adjuster->is_deleted_) {
    adjuster->reactor_.AddTimingTask(new TimingFuncRefTask<ClimbAdjuster>(
        TimingReport, adjuster, adjuster->metric_config_.report_interval_));

    adjuster->reactor_.AddTimingTask(new TimingFuncRefTask<ClimbAdjuster>(
        TimingAdjust, adjuster, adjuster->throttling_.judge_duration_));
  }
}

void ClimbAdjuster::SendInitRequest() {
  v1::MetricInitRequest* init_request = new v1::MetricInitRequest();
  init_request->mutable_key()->CopyFrom(metric_key_);
  init_request->add_dimensions()->set_type(v1::ReqCount);
  init_request->add_dimensions()->set_type(v1::LimitCount);
  init_request->add_dimensions()->set_type(v1::ErrorCount);
  v1::MetricDimension* dimension = init_request->add_dimensions();
  dimension->set_type(v1::ReqCountByDelay);
  dimension->set_value(StringUtils::TypeToStr(trigger_policy_.slow_rate_.max_rt_));
  v1::MetricInitRequest::MetricWindow* metric_window = init_request->add_windows();
  metric_window->set_duration(metric_config_.window_size_);
  metric_window->set_precision(metric_config_.precision_);
  connector_->Initialize(init_request, 1000, new MetricResponseCallback(this, kMetricRpcTypeInit));
}

void ClimbAdjuster::TimingReport(ClimbAdjuster* adjuster) {
  if (adjuster->is_deleted_) {
    return;
  }
  // 判断是否Init，如果未Init需要先进行Init
  if (adjuster->connector_->IsMetricInit(&adjuster->metric_key_)) {
    v1::MetricRequest* report_request = new v1::MetricRequest();
    v1::MetricRequest& cached_request = adjuster->report_request_;
    int64_t report_interval = static_cast<int64_t>(adjuster->metric_config_.report_interval_);
    // 没有缓存请求或不用重试缓存的请求
    if (cached_request.timestamp().value() + report_interval / 2 < adjuster->GetServerTime()) {
      cached_request.Clear();
      cached_request.mutable_key()->CopyFrom(adjuster->metric_key_);
      adjuster->call_metric_data_->Serialize(&cached_request);
      cached_request.mutable_timestamp()->set_value(adjuster->GetServerTime());
    }
    report_request->CopyFrom(cached_request);
    adjuster->connector_->Report(report_request, 1000,
                                 new MetricResponseCallback(adjuster, kMetricRpcTypeReport));
  } else {  // 还未初始化
    adjuster->SendInitRequest();
    adjuster->reactor_.AddTimingTask(
        new TimingFuncTask<ClimbAdjuster>(TimingReport, adjuster, 2000));  // 2s后立即重试
  }
}

void ClimbAdjuster::TimingAdjust(ClimbAdjuster* adjuster) {
  if (adjuster->is_deleted_) {
    return;
  }
  // 判断是否Init，如果未Init需要先进行Init
  if (adjuster->connector_->IsMetricInit(&adjuster->metric_key_)) {
    v1::MetricQueryRequest* query_request = new v1::MetricQueryRequest();
    query_request->mutable_key()->CopyFrom(adjuster->metric_key_);
    query_request->add_dimensions()->set_type(v1::ReqCount);    // 总请求数
    query_request->add_dimensions()->set_type(v1::LimitCount);  // 限流数
    query_request->add_dimensions()->set_type(v1::ErrorCount);  // 错误调用数
    v1::MetricDimension* dimension = query_request->add_dimensions();
    dimension->set_type(v1::ReqCountByDelay);  // 慢调用数
    dimension->set_value(StringUtils::TypeToStr(adjuster->trigger_policy_.slow_rate_.max_rt_));
    for (ErrorSpecialPolicies::iterator it = adjuster->trigger_policy_.error_specials_.begin();
         it != adjuster->trigger_policy_.error_specials_.end(); ++it) {
      dimension = query_request->add_dimensions();
      dimension->set_type(v1::ErrorCountByType);
      dimension->set_value(it->first);
    }
    query_request->set_duration(adjuster->metric_config_.window_size_);
    query_request->set_maxinterval(adjuster->throttling_.judge_duration_);
    adjuster->connector_->Query(query_request, adjuster->throttling_.judge_duration_ + 1000,
                                new MetricResponseCallback(adjuster, kMetricRpcTypeQuery));
  } else {  // 还未初始化
    adjuster->SendInitRequest();
    adjuster->reactor_.AddTimingTask(
        new TimingFuncTask<ClimbAdjuster>(TimingAdjust, adjuster, 2000));  // 2秒后重试
  }
}

void ClimbAdjuster::InitCallback(ReturnCode ret_code, v1::MetricResponse* response,
                                 uint64_t elapsed_time) {
  if (ret_code == kReturnOk && response->code().value() == v1::ExecuteSuccess) {
    POLARIS_LOG(LOG_DEBUG, "init metric request succ %" PRId64 "", response->timestamp().value());
    UpdateLocalTIme(response->timestamp().value(), elapsed_time);
  } else {  // 初始化失败
    if (ret_code != kReturnOk) {
      POLARIS_LOG(LOG_ERROR, "init metric request with error:%d", ret_code);
    } else {
      POLARIS_LOG(LOG_ERROR, "init metric request with rpc error:%d-%s", response->code().value(),
                  response->info().value().c_str());
    }
  }
}

void ClimbAdjuster::ReportCallback(ReturnCode ret_code, v1::MetricResponse* response,
                                   uint64_t elapsed_time) {
  uint64_t interval = 1000;
  if (ret_code != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "report metric request with error:%d", ret_code);
  } else if (response->code().value() != v1::ExecuteSuccess) {
    POLARIS_LOG(LOG_ERROR, "report metric request with rpc error:%d-%s", response->code().value(),
                response->info().value().c_str());
  } else {
    UpdateLocalTIme(response->timestamp().value(), elapsed_time);
    POLARIS_LOG(LOG_DEBUG, "report metric request succ %" PRId64 "", response->timestamp().value());
    interval = metric_config_.report_interval_;
    report_request_.Clear();  // 清空缓存的数据
  }
  reactor_.AddTimingTask(new TimingFuncTask<ClimbAdjuster>(TimingReport, this, interval));
}

void ClimbAdjuster::QueryCallback(ReturnCode ret_code, v1::MetricResponse* response) {
  if (ret_code != kReturnOk) {
    POLARIS_LOG(LOG_ERROR, "query metric request with error:%d", ret_code);
  } else if (response->code().value() != v1::ExecuteSuccess) {
    POLARIS_LOG(LOG_ERROR, "query metric request with rpc error:%d-%s", response->code().value(),
                response->info().value().c_str());
  } else {
    POLARIS_LOG(LOG_DEBUG, "query metric request success");
    if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
      POLARIS_LOG(LOG_TRACE, "query metric response %s", response->ShortDebugString().c_str());
    }
    health_metric_climb_->Update(*response);
    if (health_metric_climb_->TryAdjust(limit_amounts_)) {
      remote_bucket_->UpdateLimitAmount(limit_amounts_);
    }
  }
  // 立即出发下一次查询的任务
  reactor_.SubmitTask(new FuncRefTask<ClimbAdjuster>(TimingAdjust, this));
}

void ClimbAdjuster::UpdateLocalTIme(int64_t service_time, uint64_t elapsed_time) {
  local_time_diff_ =
      static_cast<int64_t>((Time::GetCurrentTimeMs() - elapsed_time / 2) * Time::kMillionBase) -
      service_time;
}

int64_t ClimbAdjuster::GetServerTime() {
  return static_cast<int64_t>(Time::GetCurrentTimeMs() * Time::kMillionBase) - local_time_diff_;
}

///////////////////////////////////////////////////////////////////////////////
MetricResponseCallback::MetricResponseCallback(ClimbAdjuster* adjuster, MetricRpcType rpc_type)
    : adjuster_(adjuster), rpc_type_(rpc_type) {
  adjuster_->IncrementRef();
  begin_time_ = Time::GetCurrentTimeMs();
}

MetricResponseCallback::~MetricResponseCallback() { adjuster_->DecrementRef(); }

void MetricResponseCallback::OnSuccess(v1::MetricResponse* response) {
  if (!adjuster_->IsDeleted()) {
    if (rpc_type_ == kMetricRpcTypeInit) {
      adjuster_->InitCallback(kReturnOk, response, Time::GetCurrentTimeMs() - begin_time_);
    } else if (rpc_type_ == kMetricRpcTypeReport) {
      adjuster_->ReportCallback(kReturnOk, response, Time::GetCurrentTimeMs() - begin_time_);
    } else {
      adjuster_->QueryCallback(kReturnOk, response);
    }
  }
  delete response;
}

void MetricResponseCallback::OnError(ReturnCode ret_code) {
  if (!adjuster_->IsDeleted()) {
    if (rpc_type_ == kMetricRpcTypeInit) {
      adjuster_->InitCallback(ret_code, NULL, Time::GetCurrentTimeMs() - begin_time_);
    } else if (rpc_type_ == kMetricRpcTypeReport) {
      adjuster_->ReportCallback(ret_code, NULL, Time::GetCurrentTimeMs() - begin_time_);
    } else {
      adjuster_->QueryCallback(ret_code, NULL);
    }
  }
}

}  // namespace polaris
