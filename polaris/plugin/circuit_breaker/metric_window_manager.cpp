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

#include "plugin/circuit_breaker/metric_window_manager.h"

#include <google/protobuf/stubs/port.h>
#include <google/protobuf/wrappers.pb.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <v1/circuitbreaker.pb.h>
#include <v1/code.pb.h>
#include <v1/metric.pb.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "cache/rcu_map.h"
#include "engine/circuit_breaker_executor.h"
#include "logger.h"
#include "metric/metric_connector.h"
#include "plugin/circuit_breaker/set_circuit_breaker_chain_data.h"
#include "polaris/defs.h"
#include "polaris/log.h"
#include "polaris/plugin.h"
#include "reactor/reactor.h"
#include "reactor/task.h"
#include "utils/time_clock.h"

namespace polaris {

class Context;

CbMetricBucket::CbMetricBucket() : metric_total_count_(0), metric_err_count_(0), metric_slow_count_(0) {}

CbMetricBucket::~CbMetricBucket() {
  for (auto it = specific_errs_count_.begin(); it != specific_errs_count_.end(); ++it) {
    if (it->second != nullptr) {
      delete it->second;
    }
  }
  specific_errs_count_.clear();
}

void CbMetricBucket::AddCount(const MetricReqStatus& status) {
  ++metric_total_count_;
  switch (status.status) {
    case Success:
      break;
    case Err: {
      ++metric_err_count_;
      break;
    }
    case SpecificErr: {
      auto iter = specific_errs_count_.find(status.key);
      if (iter != specific_errs_count_.end()) {
        ++(*iter->second);
      } else {
        POLARIS_LOG(POLARIS_ERROR, "no specific key:%s", status.key.c_str());
      }
      break;
    }
    case Slow: {
      ++metric_slow_count_;
      break;
    }
  }
}

MetricWindow::MetricWindow(Context* context, const ServiceKey& service_key, SubSetInfo* set_info, Labels* labels,
                           const v1::DestinationSet*& dst_set_conf, const std::string& cb_id,
                           CircuitBreakSetChainData* chain_data)
    : service_key_(service_key), is_delete_(false), cnt_(0), report_cnt_(0) {
  context_ = context;
  dst_set_conf_obj_.CopyFrom(*dst_set_conf);
  dst_set_conf_ = &dst_set_conf_obj_;
  cb_conf_id_ = cb_id;
  metric_buckets_size_ = 0;
  metric_window_ = 0;
  metric_precision_ = 0;
  bucket_duration_ = 0;

  enable_err_rate_ = false;
  enable_slow_rate_ = false;
  slow_rate_at_ = 0;

  executor_ = nullptr;
  version_ = "0";

  if (set_info != nullptr) {
    sub_set_info_.subset_map_ = set_info->subset_map_;
    sub_set_info_.subset_info_str = set_info->GetSubInfoStrId();
  }
  if (labels != nullptr) {
    labels_info_.labels_ = labels->labels_;
    labels_info_.labels_str = labels->GetLabelStr();
  }

  chain_data_ = chain_data;
  chain_data_->IncrementRef();
  report_interval_ = 1000;
  query_interval_ = 1000 * 1;
  send_req_timestamp_ = 0;
  time_diff_ = 0;
}

MetricWindow::~MetricWindow() {
  for (size_t i = 0; i < metric_buckets_.size(); ++i) {
    if (metric_buckets_[i] != nullptr) {
      delete metric_buckets_[i];
      metric_buckets_[i] = nullptr;
    }
  }
  if (dst_set_conf_ != nullptr) {
    dst_set_conf_ = nullptr;
  }
  metric_buckets_.clear();
  chain_data_->DecrementRef();
}

ReturnCode MetricWindow::InitBucket() {
  CbMetricBucket* bucket = new CbMetricBucket();
  std::map<std::string, std::set<int64_t> >::iterator it;
  for (it = specific_errors_.begin(); it != specific_errors_.end(); ++it) {
    bucket->specific_errs_count_[it->first] = new std::atomic<uint64_t>(0);
  }
  metric_buckets_.push_back(bucket);
  return kReturnOk;
}

ReturnCode MetricWindow::InitErrorConf() {
  if (dst_set_conf_->has_policy()) {
    if (dst_set_conf_->policy().has_errorrate() && dst_set_conf_->policy().errorrate().enable().value()) {
      enable_err_rate_ = true;
    }
  }
  if (enable_err_rate_) {
    v1::MetricDimension dim;
    dim.set_type(v1::ErrorCount);
    metric_dims_.push_back(dim);
    if (dst_set_conf_->policy().errorrate().specials_size() > 0) {
      const v1::CbPolicy_ErrRateConfig& err_config = dst_set_conf_->policy().errorrate();
      v1::MetricDimension dim;
      for (int i = 0; i < err_config.specials_size(); ++i) {
        const v1::CbPolicy_ErrRateConfig_SpecialConfig& sc = err_config.specials(i);
        for (int j = 0; j < sc.errorcodes_size(); ++j) {
          specific_errors_[sc.type().value()].insert(sc.errorcodes(j).value());
        }
        dim.set_type(v1::ErrorCountByType);
        dim.set_value(sc.type().value());
        metric_dims_.push_back(dim);
      }
    }
  }
  return kReturnOk;
}

ReturnCode MetricWindow::InitSlowConf() {
  if (dst_set_conf_->has_policy() && dst_set_conf_->policy().has_slowrate() &&
      dst_set_conf_->policy().slowrate().enable().value()) {
    enable_slow_rate_ = true;
  }
  if (enable_slow_rate_) {
    slow_rate_at_ = Time::DurationToUint64(dst_set_conf_->policy().slowrate().maxrt());
    v1::MetricDimension dim;
    dim.set_type(v1::ReqCountByDelay);
    dim.set_value(std::to_string(slow_rate_at_));
    metric_dims_.push_back(dim);
  }
  return kReturnOk;
}

ReturnCode MetricWindow::Init(CircuitBreakerExecutor* executor, const std::string& version) {
  version_ = version;
  if (dst_set_conf_->has_updateinterval()) {
    report_interval_ = Time::DurationToUint64(dst_set_conf_->updateinterval());
  } else {
    report_interval_ = 1000 * 20;
  }
  if (dst_set_conf_->has_metricwindow()) {
    metric_window_ = Time::DurationToUint64(dst_set_conf_->metricwindow());
  } else {
    metric_window_ = 1000 * 60;
  }
  if (dst_set_conf_->has_metricprecision()) {
    metric_precision_ = dst_set_conf_->metricprecision().value();
  } else {
    metric_precision_ = 60;
  }
  bucket_duration_ = metric_window_ / metric_precision_;
  POLARIS_LOG(POLARIS_TRACE,
              "[SET-CIRCUIT-BREAKER]{MetricWindow} init metric_window_:[%" PRIu64 "] metric_precision_:[%" PRIu64
              "] bucket_duration_:[%" PRIu64 "]",
              metric_window_, metric_precision_, bucket_duration_);
  if (dst_set_conf_->has_policy() && dst_set_conf_->policy().has_judgeduration()) {
    query_interval_ = Time::DurationToUint64(dst_set_conf_->policy().judgeduration());
  } else {
    query_interval_ = 1000 * 3;
  }

  ReturnCode return_code;
  if ((return_code = InitErrorConf()) != kReturnOk) {
    POLARIS_LOG(POLARIS_ERROR, "InitErrorConf error:%d", return_code);
    return return_code;
  }
  if ((return_code = InitSlowConf()) != kReturnOk) {
    POLARIS_LOG(POLARIS_ERROR, "InitSlowConf error:%d", return_code);
    return return_code;
  }
  if (enable_err_rate_ || enable_slow_rate_) {
    v1::MetricDimension dim;
    dim.set_type(v1::ReqCount);
    metric_dims_.push_back(dim);
  }

  metric_buckets_size_ = report_interval_ / bucket_duration_ + 3;
  POLARIS_LOG(POLARIS_TRACE, "init metric_buckets_size_:%d", metric_buckets_size_);
  for (int i = 0; i < metric_buckets_size_; ++i) {
    return_code = InitBucket();
    if (return_code != kReturnOk) {
      POLARIS_LOG(POLARIS_ERROR, "InitBucket error:%d", return_code);
      return return_code;
    }
  }

  // init metric key
  metric_key_.set_namespace_(this->service_key_.namespace_);
  metric_key_.set_service(this->service_key_.name_);
  metric_key_.set_subset(this->sub_set_info_.GetSubInfoStrId());
  metric_key_.set_labels(this->labels_info_.GetLabelStr());
  metric_key_.set_role(v1::MetricKey_Role_Caller);

  executor_ = executor;
  POLARIS_ASSERT(executor_ != nullptr);
  if (dst_set_conf_->type() == v1::DestinationSet_Type_GLOBAL) {
    executor->GetReactor().SubmitTask(new FuncRefTask<MetricWindow>(TimingMetricReport, this));
    executor->GetReactor().SubmitTask(new FuncRefTask<MetricWindow>(TimingMetricQuery, this));
  }
  return kReturnOk;
}

void MetricWindow::TimingMetricReport(MetricWindow* window) {
  if (window->is_delete_) {
    POLARIS_LOG(POLARIS_DEBUG, "set circuit breaker timing report but window delete");
    return;
  }
  uint64_t report_interval = window->report_interval_;
  if (window->executor_->GetMetricConnector()->IsMetricInit(&window->metric_key_)) {
    v1::MetricRequest* report_request = window->AssembleReportReq();
    MetricReportCallBack* callback = new MetricReportCallBack(window, *report_request);
    ReturnCode ret_code = window->executor_->GetMetricConnector()->Report(report_request, 1000, callback);
    if (ret_code != kReturnOk) {
      POLARIS_LOG(POLARIS_ERROR, "set circuit breaker timing report with error:%d", ret_code);
    }
  } else {
    AsyncInit(window);
    report_interval = 2000;  // 2s后重试
  }
  window->executor_->GetReactor().AddTimingTask(
      new TimingFuncRefTask<MetricWindow>(TimingMetricReport, window, report_interval));
}

void MetricWindow::TimingMetricQuery(MetricWindow* window) {
  if (window->is_delete_) {
    POLARIS_LOG(POLARIS_DEBUG, "set circuit breaker timing query but window delete");
    return;
  }
  if (window->executor_->GetMetricConnector()->IsMetricInit(&window->metric_key_)) {
    ReturnCode ret_code = window->MetricQuery();
    if (ret_code != kReturnOk) {
      POLARIS_LOG(POLARIS_ERROR, "set circuit breaker timing query with error:%d", ret_code);
    }
  } else {
    AsyncInit(window);
    // 还未初始化2s后重试
    window->executor_->GetReactor().AddTimingTask(new TimingFuncRefTask<MetricWindow>(TimingMetricQuery, window, 2000));
  }
}

v1::MetricInitRequest* MetricWindow::AssembleInitReq() {
  v1::MetricInitRequest* req = new v1::MetricInitRequest();
  v1::MetricKey* metric_key = req->mutable_key();
  metric_key->CopyFrom(metric_key_);

  for (size_t i = 0; i < metric_dims_.size(); ++i) {
    v1::MetricDimension* d = req->mutable_dimensions()->Add();
    d->set_type(metric_dims_[i].type());
    d->set_value(metric_dims_[i].value());
  }

  v1::MetricInitRequest_MetricWindow* window = req->mutable_windows()->Add();
  window->set_duration(this->metric_window_);
  window->set_precision(this->metric_precision_);
  return req;
}

void MetricWindow::AsyncInit(MetricWindow* metric_window) {
  if (metric_window->is_delete_) {
    return;
  }
  v1::MetricInitRequest* req = metric_window->AssembleInitReq();
  ReturnCode return_code;
  MetricInitCallBack* callback = new MetricInitCallBack(metric_window);
  metric_window->send_req_timestamp_ = Time::GetSystemTimeMs();
  POLARIS_ASSERT(metric_window->executor_->GetMetricConnector() != nullptr);
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    POLARIS_LOG(POLARIS_TRACE, "[SET-CIRCUIT-BREAKER]{MetricWindow} MetricInit request:%s",
                req->ShortDebugString().c_str());
  }
  return_code = metric_window->executor_->GetMetricConnector()->Initialize(req, 2 * 1000, callback);
  if (return_code != kReturnOk) {
    POLARIS_LOG(POLARIS_ERROR, "set circuit breaker metric key[%s] init with error:%d",
                metric_window->metric_key_.ShortDebugString().c_str(), return_code);
  }
  return;
}

v1::MetricRequest* MetricWindow::AssembleReportReq() {
  v1::MetricRequest* req = new v1::MetricRequest();
  v1::MetricKey* metric_key = req->mutable_key();
  metric_key->CopyFrom(metric_key_);

  uint64_t time_now = Time::GetSystemTimeMs();
  uint64_t bucket_time = time_now / bucket_duration_;
  int index = bucket_time % metric_buckets_size_;

  uint64_t m_count = metric_buckets_.size();

  v1::MetricRequest_MetricIncrement* incr = req->mutable_increments()->Add();
  incr->set_duration(metric_window_);
  incr->set_precision(metric_precision_);

  uint64_t count_cnt = 0;
  int64_t idx = index;
  std::vector<uint64_t> req_buckets;
  std::vector<uint64_t> err_buckets;
  std::vector<uint64_t> slow_buckets;
  std::map<std::string, std::vector<uint64_t> > specific_key_buckets;
  while (count_cnt < m_count) {
    uint64_t b_total_count = metric_buckets_[idx]->metric_total_count_.exchange(0);
    req_buckets.push_back(b_total_count);

    uint64_t b_err_count = metric_buckets_[idx]->metric_err_count_.exchange(0);
    err_buckets.push_back(b_err_count);

    for (auto iter = metric_buckets_[idx]->specific_errs_count_.begin();
         iter != metric_buckets_[idx]->specific_errs_count_.end(); ++iter) {
      uint64_t count = iter->second->exchange(0);
      specific_key_buckets[iter->first].push_back(count);
      POLARIS_LOG(POLARIS_TRACE,
                  "set circuit breaker report count:[%" PRIu64 "] idx:%" PRId64
                  ", specific_type:%s specific_count:[%" PRIu64 "]",
                  count, idx, iter->first.c_str(), count);
    }

    uint64_t slow_count = metric_buckets_[idx]->metric_slow_count_.exchange(0);
    slow_buckets.push_back(slow_count);

    POLARIS_LOG(POLARIS_TRACE,
                "[SET-CIRCUIT-BREAKER]{MetricWindow} MetricReport count:[%" PRIu64 "] idx:%" PRIu64 ", total:[%" PRId64
                "] err_count:[%" PRIu64 "] slow_count:[%" PRIu64 "]",
                count_cnt, idx, b_total_count, b_err_count, slow_count);
    ++count_cnt;
    --idx;
    if (idx < 0) {
      idx = metric_buckets_.size() - 1;
    }
  }

  v1::MetricRequest_MetricIncrement_Values* req_values = incr->mutable_values()->Add();
  req_values->mutable_dimension()->set_type(v1::ReqCount);
  req_values->mutable_dimension()->set_value("");
  for (size_t i = 0; i < req_buckets.size(); ++i) {
    req_values->mutable_values()->Add(req_buckets[i]);
    report_cnt_ += req_buckets[i];
  }
  POLARIS_LOG(POLARIS_TRACE, "[SET-CIRCUIT-BREAKER]{MetricWindow} MetricReport report_count:[%" PRIu64 "]",
              report_cnt_.load());

  v1::MetricRequest_MetricIncrement_Values* err_values = incr->mutable_values()->Add();
  err_values->mutable_dimension()->set_type(v1::ErrorCount);
  err_values->mutable_dimension()->set_value("");
  for (size_t i = 0; i < err_buckets.size(); ++i) {
    err_values->mutable_values()->Add(err_buckets[i]);
  }

  // specific err
  std::map<std::string, std::vector<uint64_t> >::iterator iter;
  for (iter = specific_key_buckets.begin(); iter != specific_key_buckets.end(); ++iter) {
    v1::MetricRequest_MetricIncrement_Values* sp_err_values = incr->mutable_values()->Add();
    sp_err_values->mutable_dimension()->set_type(v1::ErrorCountByType);
    sp_err_values->mutable_dimension()->set_value(iter->first);
    for (size_t i = 0; i < iter->second.size(); ++i) {
      sp_err_values->mutable_values()->Add(iter->second[i]);
    }
  }

  // slow
  v1::MetricRequest_MetricIncrement_Values* slow_values = incr->mutable_values()->Add();
  slow_values->mutable_dimension()->set_type(v1::ReqCountByDelay);
  slow_values->mutable_dimension()->set_value(std::to_string(slow_rate_at_));
  for (size_t i = 0; i < slow_buckets.size(); ++i) {
    slow_values->mutable_values()->Add(slow_buckets[i]);
  }
  req->mutable_timestamp()->set_value((time_now + time_diff_) * Time::kMillionBase);
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    POLARIS_LOG(POLARIS_TRACE, "[SET-CIRCUIT-BREAKER]{MetricWindow} MetricReport request:%s",
                req->ShortDebugString().c_str());
  }
  return req;
}

ReturnCode MetricWindow::MetricQuery() {
  std::string subset = this->sub_set_info_.GetSubInfoStrId();
  if (subset.empty()) {
    subset = "*";
  }
  std::string label = this->labels_info_.GetLabelStr();
  if (label.empty()) {
    label = "*";
  }
  v1::MetricQueryRequest* req = new v1::MetricQueryRequest();
  v1::MetricKey* metric_key = req->mutable_key();
  metric_key->set_namespace_(service_key_.namespace_);
  metric_key->set_service(service_key_.name_);
  metric_key->set_subset(subset);
  metric_key->set_labels(label);
  req->set_duration(this->metric_window_);
  req->set_maxinterval(this->query_interval_);
  for (size_t i = 0; i < this->metric_dims_.size(); ++i) {
    v1::MetricDimension* dim = req->mutable_dimensions()->Add();
    dim->CopyFrom(this->metric_dims_[i]);
  }
  return executor_->GetMetricConnector()->Query(req, this->query_interval_ + 1000, new MetricQueryCallback(this));
}

ReturnCode MetricWindow::MetricReportWithCallBack(void* callback) {
  MetricReportCallBack* cb = static_cast<MetricReportCallBack*>(callback);
  v1::MetricRequest* req = new v1::MetricRequest();
  req->CopyFrom(cb->GetRequest());
  return executor_->GetMetricConnector()->Report(req, 2 * 1000, cb);
}

ReturnCode MetricWindow::AddCount(const InstanceGauge& gauge) {
  MetricReqStatus status;
  status.status = Success;
  if (gauge.call_ret_status == kCallRetOk) {
    if (enable_slow_rate_ && gauge.call_daley >= slow_rate_at_) {
      status.status = Slow;
    }
  } else if (gauge.call_ret_status == kCallRetError) {
    if (enable_err_rate_) {
      std::map<std::string, std::set<int64_t> >::iterator iter;
      std::string specific_str = "";
      for (iter = specific_errors_.begin(); iter != specific_errors_.end(); ++iter) {
        if (iter->second.find(gauge.call_ret_code) != iter->second.end()) {
          specific_str = iter->first;
          break;
        }
      }
      if (specific_str.empty()) {
        status.status = Err;
      } else {
        status.status = SpecificErr;
        status.key = specific_str;
      }
    }
  } else {
    return kReturnOk;
  }
  ++cnt_;
  uint64_t time_now = Time::GetSystemTimeMs();
  uint64_t bucket_time = time_now / bucket_duration_;
  uint64_t index = bucket_time % metric_buckets_size_;
  CbMetricBucket* bucket = metric_buckets_[index];
  POLARIS_LOG(POLARIS_TRACE,
              "[SET-CIRCUIT-BREAKER]{AddCount} bucket_duration_:[%" PRIu64 "], bucket_time:[%" PRIu64
              "], index:[%" PRIu64 "]",
              bucket_duration_, bucket_time, index);
  POLARIS_LOG(POLARIS_TRACE, "[SET-CIRCUIT-BREAKER]{MetricWindow} AddCount is status:%d", status.status);
  bucket->AddCount(status);
  return kReturnOk;
}

void MetricWindow::InitCallback(v1::MetricResponse* response) {
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    POLARIS_LOG(POLARIS_TRACE, "set circuit breaker init response:%s", response->ShortDebugString().c_str());
  }
  if (response->code().value() != v1::ExecuteSuccess) {
    POLARIS_LOG(POLARIS_TRACE, "set circuit breaker init response with error:%d", response->code().value());
  } else {
    int64_t time_now = static_cast<int64_t>(Time::GetSystemTimeMs());
    int64_t net_bound = time_now > send_req_timestamp_ ? (time_now - send_req_timestamp_) / 2 : 0;
    time_diff_ = response->timestamp().value() / Time::kMillionBase - net_bound - send_req_timestamp_;
    POLARIS_LOG(POLARIS_TRACE,
                "set circuit breaker init servertime:[%" PRId64 "] local_init_time:[%" PRId64 "] time_diff: [%" PRId64
                "] net_bound: [%" PRId64 "]",
                response->timestamp().value() / Time::kMillionBase, send_req_timestamp_, time_diff_, net_bound);
  }
  delete response;
}

void MetricWindow::QueryCallback(ReturnCode ret_code, v1::MetricResponse* response) {
  if (ret_code != kReturnOk) {
    POLARIS_LOG(POLARIS_ERROR, "set circuit breaker metric query with error:%d", ret_code);
  } else {
    if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
      POLARIS_LOG(POLARIS_TRACE, "set circuit breaker metric query with response:%s",
                  response->ShortDebugString().c_str());
    }
    if (response->code().value() == v1::ExecuteSuccess) {
      ReturnCode ret_code = chain_data_->JudgeAndTranslateStatus(*response, GetWindowKey(), dst_set_conf_, cb_conf_id_);
      if (ret_code != kReturnOk) {
        POLARIS_LOG(POLARIS_ERROR, "set circuit breaker judge and translate status with error:%d", ret_code);
      }
    }
    delete response;
  }
  // 立即发起下一次查询任务
  executor_->GetReactor().SubmitTask(new FuncRefTask<MetricWindow>(TimingMetricQuery, this));
}

std::string MetricWindow::GetWindowKey() { return sub_set_info_.GetSubInfoStrId() + "#" + labels_info_.GetLabelStr(); }

///////////////////////////////////////////////////////////////////////////////
MetricWindowManager::MetricWindowManager(Context* context, CircuitBreakerExecutor* executor) {
  context_ = context;
  executor_ = executor;
  windows_ = new RcuMap<std::string, MetricWindow>();
}

MetricWindowManager::~MetricWindowManager() {
  if (windows_ != nullptr) {
    delete windows_;
  }
}

MetricWindow* MetricWindowManager::GetWindow(SubSetInfo& subset, Labels& labels) {
  std::string window_key = subset.GetSubInfoStrId() + "#" + labels.GetLabelStr();
  return windows_->Get(window_key);
}

MetricWindow* MetricWindowManager::UpdateWindow(const ServiceKey& service_key, SubSetInfo& subset, Labels& labels,
                                                const std::string& version, const v1::DestinationSet* dst_set_conf,
                                                const std::string& cb_id, CircuitBreakSetChainData* chain_data) {
  const std::lock_guard<std::mutex> mutex_guard(update_lock_);
  std::string window_key = subset.GetSubInfoStrId() + "#" + labels.GetLabelStr();
  MetricWindow* window = windows_->Get(window_key);
  if (window == nullptr || window->GetVersion() != version) {
    if (window != nullptr) {
      window->MarkDeleted();
      window->DecrementRef();
    }
    window = new MetricWindow(context_, service_key, &subset, &labels, dst_set_conf, cb_id, chain_data);
    window->Init(executor_, version);
    window->IncrementRef();
    windows_->Update(window_key, window);

    if (dst_set_conf->scope() == v1::DestinationSet_Scope_ALL) {
      std::string subset_window_key = subset.GetSubInfoStrId() + "#";
      MetricWindow* subset_win = windows_->Get(subset_window_key);
      if (subset_win == nullptr || subset_win->GetVersion() != version) {
        if (subset_win != nullptr) {
          subset_win->MarkDeleted();
          subset_win->DecrementRef();
        }
        subset_win = new MetricWindow(context_, service_key, &subset, nullptr, dst_set_conf, cb_id, chain_data);
        subset_win->Init(executor_, version);
        windows_->Update(subset_window_key, subset_win);
      } else {
        subset_win->DecrementRef();
      }
    }
  }
  return window;
}

void MetricWindowManager::WindowGc() { windows_->CheckGc(1000); }

///////////////////////////////////////////////////////////////////////////////
MetricInitCallBack::MetricInitCallBack(MetricWindow* window) : window_(window) { window_->IncrementRef(); }

MetricInitCallBack::~MetricInitCallBack() { window_->DecrementRef(); }

void MetricInitCallBack::OnSuccess(v1::MetricResponse* response) {
  if (window_->IsDeleted()) {
    delete response;
    return;
  }
  window_->InitCallback(response);
  return;
}

void MetricInitCallBack::OnError(ReturnCode ret_code) {
  if (window_->IsDeleted()) {
    return;
  }
  POLARIS_LOG(POLARIS_ERROR, "set circuit metric init response with error:%d", ret_code);
}

///////////////////////////////////////////////////////////////////////////////
MetricReportCallBack::MetricReportCallBack(MetricWindow* window, v1::MetricRequest& req)
    : window_(window), try_times_(1) {
  window_->IncrementRef();
  request_.CopyFrom(req);
}

MetricReportCallBack::~MetricReportCallBack() { window_->DecrementRef(); }

void MetricReportCallBack::OnSuccess(v1::MetricResponse* response) {
  if (window_->IsDeleted()) {
    delete response;
    return;
  }
  if (POLARIS_LOG_ENABLE(kTraceLogLevel)) {
    POLARIS_LOG(POLARIS_TRACE, "set circiut breaker report with response:%s", response->ShortDebugString().c_str());
  }
  if (response->code().value() != v1::ExecuteSuccess) {
    uint32_t rsp_code = response->code().value();
    uint32_t err_type = rsp_code / 1000;
    if (err_type == 500 && try_times_ < 3) {
      MetricReportCallBack* cb = new MetricReportCallBack(window_, request_);
      cb->try_times_ = try_times_ + 1;
      ReturnCode ret = window_->MetricReportWithCallBack(cb);
      if (ret != kReturnOk) {
        POLARIS_LOG(POLARIS_ERROR, "set circuit breaker retry report with error:%d", ret);
      }
    }
  }
  delete response;
}

void MetricReportCallBack::OnError(ReturnCode ret_code) {
  if (window_->IsDeleted()) {
    return;
  }
  POLARIS_LOG(POLARIS_ERROR, "set circuit breaker metric report with error:%d", ret_code);
}

///////////////////////////////////////////////////////////////////////////////
MetricQueryCallback::MetricQueryCallback(MetricWindow* window) : window_(window) { window_->IncrementRef(); }

MetricQueryCallback::~MetricQueryCallback() { window_->DecrementRef(); }

void MetricQueryCallback::OnSuccess(v1::MetricResponse* response) {
  if (!window_->IsDeleted()) {
    window_->QueryCallback(kReturnOk, response);
    return;
  }
}

void MetricQueryCallback::OnError(ReturnCode ret_code) {
  if (!window_->IsDeleted()) {
    window_->QueryCallback(ret_code, nullptr);
  }
}

}  // namespace polaris
