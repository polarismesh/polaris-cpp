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

#include "plugin/circuit_breaker/set_circuit_breaker_chain_data.h"

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/stubs/port.h>
#include <google/protobuf/wrappers.pb.h>
#include <inttypes.h>
#include <v1/circuitbreaker.pb.h>
#include <v1/metric.pb.h>

#include <sstream>
#include <string>

#include "logger.h"
#include "monitor/service_record.h"
#include "plugin/circuit_breaker/chain.h"
#include "polaris/log.h"
#include "utils/time_clock.h"

namespace polaris {

class MetricWindowManager;

CircuitBreakSetChainData::CircuitBreakSetChainData(ServiceKey& service_key, LocalRegistry* local_registry,
                                                   MetricWindowManager* window_manager, ServiceRecord* service_record)
    : is_deleted_(false), service_key_(service_key), version_(0) {
  local_registry_ = local_registry;
  pthread_rwlock_init(&rwlock_, nullptr);
  windows_info_version_ = 0;
  windows_manager_ = window_manager;
  service_record_ = service_record;
}

CircuitBreakSetChainData::~CircuitBreakSetChainData() {
  std::map<std::string, SetCircuitBreakerUnhealthyInfo*>::iterator iter;
  for (iter = unhealthy_sets_.begin(); iter != unhealthy_sets_.end(); ++iter) {
    if (iter->second != nullptr) {
      delete iter->second;
    }
  }
  unhealthy_sets_.clear();
}

void CircuitBreakSetChainData::ComputeTypeCount(const v1::MetricResponse& resp, uint64_t* total_count,
                                                uint64_t* err_count, uint64_t* slow_count,
                                                std::map<std::string, uint64_t>* specific_err_) {
  for (int i = 0; i < resp.summaries().size(); ++i) {
    const v1::MetricResponse_MetricSum& sum = resp.summaries(i);
    for (int j = 0; j < sum.values_size(); ++j) {
      switch (sum.values(j).dimension().type()) {
        case v1::ReqCount:
          *total_count += sum.values(j).value();
          break;
        case v1::ReqCountByDelay:
          *slow_count += sum.values(j).value();
          break;
        case v1::ErrorCount:
          *err_count += sum.values(j).value();
          break;
        case v1::ErrorCountByType:
          (*specific_err_)[sum.values(j).dimension().value()] += sum.values(j).value();
          break;
        default:
          break;
      }
    }
  }
}

ReturnCode CircuitBreakSetChainData::ComputeUnhealthyInfo(const v1::MetricResponse& resp,
                                                          const v1::DestinationSet* conf,
                                                          CircuitBreakerSetComputeResult* info) {
  if (!conf->has_policy()) {
    return kReturnOk;
  }
  uint64_t total_count = 0;
  uint64_t err_count = 0;
  uint64_t slow_count = 0;
  std::map<std::string, uint64_t> specific_err_;
  ComputeTypeCount(resp, &total_count, &err_count, &slow_count, &specific_err_);
  info->total_count = total_count;
  POLARIS_LOG(POLARIS_TRACE,
              "set circuit breaker response total_count:[%" PRIu64 "], err_count:[%" PRIu64 "], slow_count:[%" PRIu64
              "]",
              total_count, err_count, slow_count);
  if (total_count == 0) {
    return kReturnOk;
  }
  info->status = kCircuitBreakerClose;
  const v1::CbPolicy& policy = conf->policy();
  if (policy.has_errorrate() && policy.errorrate().enable().value()) {
    // 判断总请求数是否达到阀值
    if (total_count >= policy.errorrate().requestvolumethreshold().value()) {
      uint64_t err_rate = (err_count * 100) / total_count;
      if (err_rate >= policy.errorrate().errorratetoopen().value()) {
        info->status = kCircuitBreakerOpen;
      } else if (err_rate >= policy.errorrate().errorratetopreserved().value()) {
        info->status = kCircuitBreakerPreserved;
      }
      info->status_reason = "cased by err_rate";
      // 已经触发熔断就不用继续检查了
      if (info->status == kCircuitBreakerOpen) {
        return kReturnOk;
      }
      // 检查 specific error
      const v1::CbPolicy_ErrRateConfig& err_conf = policy.errorrate();
      std::map<std::string, uint64_t>::iterator iter;
      for (int i = 0; i < err_conf.specials_size(); ++i) {
        iter = specific_err_.find(err_conf.specials(i).type().value());
        if (iter == specific_err_.end()) {
          continue;
        }
        uint64_t err_rate = (iter->second * 100) / total_count;
        if (err_rate >= err_conf.specials(i).errorratetoopen().value()) {
          info->status = kCircuitBreakerOpen;
          info->status_reason = "cased by specific_err";
          return kReturnOk;
        } else if (err_rate >= err_conf.specials(i).errorratetopreserved().value()) {
          info->status = kCircuitBreakerPreserved;
          info->status_reason = "cased by specific_err";
        }
      }
    }
  }
  if (info->status == kCircuitBreakerOpen) {
    return kReturnOk;
  }
  if (policy.has_slowrate() && policy.slowrate().enable().value()) {
    const v1::CbPolicy_SlowRateConfig& slow_conf = policy.slowrate();
    uint64_t slow_rate = (slow_count * 100) / total_count;
    if (slow_conf.has_slowratetoopen() && slow_conf.slowratetoopen().value() != 0 &&
        slow_rate >= slow_conf.slowratetoopen().value()) {
      info->status = kCircuitBreakerOpen;
      info->status_reason = "cased by slow_rate";
    } else if (slow_conf.has_slowratetopreserved() && slow_conf.slowratetopreserved().value() != 0 &&
               slow_rate >= slow_conf.slowratetopreserved().value()) {
      info->status = kCircuitBreakerPreserved;
      info->status_reason = "cased by slow_rate";
    }
  }
  return kReturnOk;
}

bool CircuitBreakSetChainData::JudgeOpenTranslate(SetCircuitBreakerUnhealthyInfo* info, const v1::DestinationSet* conf,
                                                  uint64_t time_now) {
  POLARIS_LOG(POLARIS_TRACE,
              "set circuit breaker translate time_now:[%" PRIu64 "], open_status_begin_time:[%" PRIu64
              "], sleep_window:[%" PRId64 "]",
              time_now, info->open_status_begin_time, conf->recover().sleepwindow().seconds());
  // 检查是否转为半开
  uint64_t sleep_window;
  if (conf->recover().has_sleepwindow()) {
    sleep_window = Time::DurationToUint64(conf->recover().sleepwindow());
  } else {
    sleep_window = 1000 * 600;
  }
  if (time_now < info->open_status_begin_time) {
    return false;
  }
  if ((time_now - info->open_status_begin_time) >= sleep_window) {
    info->status = kCircuitBreakerHalfOpen;
    if (conf->recover().requestrateafterhalfopen_size() > 0) {
      google::protobuf::uint32 v = conf->recover().requestrateafterhalfopen(0).value();
      info->half_open_release_percent = v / 100.0;
    } else {
      info->half_open_release_percent = 1;
    }
    info->last_half_open_release_time = time_now;
    return true;
  }
  return false;
}

bool CircuitBreakSetChainData::JudgeHalfOpenTranslate(SetCircuitBreakerUnhealthyInfo* info,
                                                      CircuitBreakerSetComputeResult* new_info,
                                                      const v1::DestinationSet* conf, uint64_t time_now) {
  // 检查是否可以关闭
  const v1::RecoverConfig& recover = conf->recover();
  int size = recover.requestrateafterhalfopen_size();

  uint64_t time_interval =
      conf->has_metricwindow() ? Time::DurationToUint64(conf->metricwindow()) : 60 * Time::kThousandBase;
  if (new_info->status == kCircuitBreakerClose && new_info->total_count > 0) {
    if (time_now - info->last_half_open_release_time < time_interval) {
      return false;
    }
    if (size > 0) {
      int idx = 0;
      for (idx = 0; idx < size; ++idx) {
        if (recover.requestrateafterhalfopen(idx).value() > info->half_open_release_percent * 100) {
          break;
        }
      }
      if (idx == size) {
        info->status = kCircuitBreakerClose;
        return true;
      } else {
        // 继续放量
        info->half_open_release_percent = recover.requestrateafterhalfopen(idx).value() / 100.0;
        info->last_half_open_release_time = time_now;
        return true;
      }
    } else {
      if (info->half_open_release_percent > 0) {
        info->status = kCircuitBreakerClose;
        return true;
      }
      info->half_open_release_percent = 1.0;
      info->last_half_open_release_time = time_now;
    }
  } else if (new_info->status == kCircuitBreakerOpen) {
    info->status = kCircuitBreakerOpen;
    info->half_open_release_percent = 0;
    info->open_status_begin_time = time_now;
    return true;
  }

  return false;
}

bool CircuitBreakSetChainData::JudgePreservedTranslate(SetCircuitBreakerUnhealthyInfo* info,
                                                       CircuitBreakerSetComputeResult* new_info, uint64_t time_now) {
  if (new_info->status == kCircuitBreakerClose && new_info->total_count != 0) {
    info->status = kCircuitBreakerClose;
    return true;
  } else if (new_info->status == kCircuitBreakerOpen) {
    info->status = kCircuitBreakerOpen;
    info->half_open_release_percent = 0;
    info->open_status_begin_time = time_now;
    return true;
  }
  return false;
}

// 判断熔断状态入口
ReturnCode CircuitBreakSetChainData::JudgeAndTranslateStatus(const v1::MetricResponse& resp,
                                                             const std::string& set_label_id,
                                                             const v1::DestinationSet* conf, const std::string& cb_id) {
  CircuitBreakerSetComputeResult new_info;
  ReturnCode ret_code = ComputeUnhealthyInfo(resp, conf, &new_info);
  POLARIS_LOG(POLARIS_TRACE, "set circuit breaker compute unhealthy info %s status:%d", set_label_id.c_str(),
              new_info.status);
  if (ret_code != kReturnOk) {
    return ret_code;
  }
  uint64_t time_now = resp.timestamp().value() / Time::kMillionBase;
  if (conf->scope() == v1::DestinationSet_Scope_LABELS) {
    ret_code = ChangeSubsetOneLabel(&new_info, conf, time_now, set_label_id, cb_id);
  } else if (conf->scope() == v1::DestinationSet_Scope_ALL) {
    if (new_info.status == kCircuitBreakerOpen) {
      // 熔断set下所有
      ret_code = CircuitBreakSubsetAll(set_label_id, time_now, cb_id, &new_info);
      if (ret_code != kReturnOk) {
        return ret_code;
      }
      ret_code = ChangeSubsetOneLabel(&new_info, conf, time_now, set_label_id, cb_id);
    } else {
      ret_code = ChangeSubsetOneLabel(&new_info, conf, time_now, set_label_id, cb_id);
    }
  } else {
    POLARIS_LOG(POLARIS_ERROR, "JudgeAndTranslateStatus not support scope:%d", conf->scope());
  }
  return ret_code;
}

ReturnCode CircuitBreakSetChainData::ChangeSubsetOneLabel(CircuitBreakerSetComputeResult* new_info,
                                                          const v1::DestinationSet* conf, uint64_t time_now,
                                                          const std::string& set_label_id, const std::string& cb_id) {
  std::map<std::string, SetCircuitBreakerUnhealthyInfo*>::iterator iter;
  iter = unhealthy_sets_.find(set_label_id);
  CircuitChangeRecord* change_record;
  if (iter == unhealthy_sets_.end()) {
    if (new_info->status != kCircuitBreakerClose) {
      SetCircuitBreakerUnhealthyInfo* info = new SetCircuitBreakerUnhealthyInfo();
      info->status = new_info->status;
      info->half_open_release_percent = 0;
      info->open_status_begin_time = time_now;
      unhealthy_sets_[set_label_id] = info;
      ++version_;
      POLARIS_LOG(POLARIS_TRACE,
                  "set circuit breaker judge change subset one label add unhealthy set "
                  "time now:[%" PRIu64 "] set_label_id:%s status:%d, version:[%" PRIu64 "]",
                  time_now, set_label_id.c_str(), info->status, version_.load());
      change_record =
          ChangeRecordValues(set_label_id, time_now, kCircuitBreakerClose, new_info->status, new_info->status_reason);
      change_record->circuit_breaker_conf_id_ = cb_id;
      service_record_->SetCircuitBreak(service_key_, set_label_id, change_record);
    }
  } else {
    SetCircuitBreakerUnhealthyInfo* info = iter->second;
    bool change = false;
    CircuitBreakerStatus old_status = info->status;
    if (info->status == kCircuitBreakerOpen) {
      change = JudgeOpenTranslate(info, conf, time_now);
    } else if (info->status == kCircuitBreakerHalfOpen) {
      change = JudgeHalfOpenTranslate(info, new_info, conf, time_now);
    } else if (info->status == kCircuitBreakerPreserved) {
      change = JudgePreservedTranslate(info, new_info, time_now);
    }
    if (change == true) {
      CircuitBreakerStatus new_status = info->status;
      if (info->status == kCircuitBreakerClose) {
        delete iter->second;
        unhealthy_sets_.erase(iter);
      }
      ++version_;
      POLARIS_LOG(POLARIS_TRACE, "set circuit breaker judge change subset one label version: [%" PRIu64 "]",
                  version_.load());
      change_record = ChangeRecordValues(set_label_id, time_now, old_status, new_status, new_info->status_reason);
      change_record->circuit_breaker_conf_id_ = cb_id;
      service_record_->SetCircuitBreak(service_key_, set_label_id, change_record);
    }
  }
  return kReturnOk;
}

ReturnCode CircuitBreakSetChainData::CircuitBreakSubsetAll(const std::string& set_label_id, uint64_t time_now,
                                                           const std::string& cb_id,
                                                           CircuitBreakerSetComputeResult* new_info) {
  std::string::size_type split_idx = set_label_id.find("#");
  std::string origin_subset = set_label_id.substr(0, split_idx);
  bool change = false;
  std::string key = origin_subset + "#";
  std::map<std::string, SetCircuitBreakerUnhealthyInfo*>::iterator iter;
  iter = unhealthy_sets_.find(key);
  std::string change_reason;
  CircuitChangeRecord* change_record;
  if (iter == unhealthy_sets_.end()) {
    SetCircuitBreakerUnhealthyInfo* info = new SetCircuitBreakerUnhealthyInfo();
    info->status = kCircuitBreakerOpen;
    info->half_open_release_percent = 0;
    info->open_status_begin_time = time_now;
    unhealthy_sets_[key] = info;
    change = true;
    change_record =
        ChangeRecordValues(set_label_id, time_now, kCircuitBreakerClose, kCircuitBreakerOpen, new_info->status_reason);
  } else {
    if (iter->second->status != kCircuitBreakerOpen) {
      CircuitBreakerStatus old_status = iter->second->status;
      iter->second->status = kCircuitBreakerOpen;
      iter->second->half_open_release_percent = 0;
      iter->second->open_status_begin_time = time_now;
      change = true;
      change_record =
          ChangeRecordValues(set_label_id, time_now, old_status, kCircuitBreakerOpen, new_info->status_reason);
    }
  }
  if (change) {
    ++version_;
    change_record->change_seq_ = version_.load();
    change_record->circuit_breaker_conf_id_ = cb_id;
    service_record_->SetCircuitBreak(service_key_, key, change_record);
  }
  return kReturnOk;
}

ReturnCode CircuitBreakSetChainData::CheckAndSyncToRegistry() {
  CircuitBreakUnhealthySetsData unhealthy_sets_data;
  unhealthy_sets_data.version = version_.load();
  std::map<std::string, SetCircuitBreakerUnhealthyInfo*>::iterator iter;
  for (iter = unhealthy_sets_.begin(); iter != unhealthy_sets_.end(); ++iter) {
    unhealthy_sets_data.subset_unhealthy_infos[iter->first] = *iter->second;
  }
  return local_registry_->UpdateSetCircuitBreakerData(service_key_, unhealthy_sets_data);
}

CircuitChangeRecord* CircuitBreakSetChainData::ChangeRecordValues(const std::string& set_label_id, uint64_t change_time,
                                                                  CircuitBreakerStatus from, CircuitBreakerStatus to,
                                                                  std::string& status_reason) {
  CircuitChangeRecord* record = new CircuitChangeRecord();
  record->change_time_ = change_time;
  record->from_ = from;
  record->to_ = to;
  std::ostringstream output;
  output << set_label_id << " " << CircuitBreakerStatusToStr(from) << " to " << CircuitBreakerStatusToStr(to) << " "
         << status_reason;
  record->reason_ = output.str();
  return record;
}

}  // namespace polaris
