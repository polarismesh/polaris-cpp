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

#include "plugin/stat_reporter/stat_reporter.h"

#include <sstream>
#include <utility>

#include "logger.h"
#include "polaris/config.h"

namespace polaris {

class Context;

std::string InstanceCodeStat::ToString() {
  std::stringstream ss;
  ss << "succ_count[" << success_count_ << "], succ_avg_delay["
     << (success_count_ > 0 ? success_delay_ / success_count_ : 0) << "], err_count[" << error_count_
     << "], err_avg_delay[" << (error_count_ > 0 ? error_delay_ / error_count_ : 0) << "]";
  return ss.str();
}

namespace StatReporterConfig {
static const char kReportIntervalKey[] = "reportInterval";
static const uint64_t kReportIntervalDefault = 60 * 1000;
}  // namespace StatReporterConfig

MonitorStatReporter::MonitorStatReporter() {
  context_ = nullptr;
  perpare_time_ = 0;
  report_interval_ = 0;
  tls_key_ = 0;
  int rc = pthread_key_create(&tls_key_, &OnThreadExit);
  POLARIS_ASSERT(rc == 0);
}

MonitorStatReporter::~MonitorStatReporter() {
  context_ = nullptr;
  pthread_key_delete(tls_key_);
  for (auto tls_stat : tls_stat_set_) {
    delete tls_stat;
  }
  for (auto data : perpare_data_) {
    delete data;
  }
}

ReturnCode MonitorStatReporter::Init(Config* config, Context* context) {
  context_ = context;
  report_interval_ =
      config->GetMsOrDefault(StatReporterConfig::kReportIntervalKey, StatReporterConfig::kReportIntervalDefault);
  POLARIS_CHECK(report_interval_ > 0, kReturnInvalidConfig);
  return kReturnOk;
}

ReturnCode MonitorStatReporter::ReportStat(const InstanceGauge& instance_gauge) {
  TlsInstanceStat* thread_stat = static_cast<TlsInstanceStat*>(pthread_getspecific(tls_key_));
  if (thread_stat != nullptr) {
    thread_stat->access_time_.store(Time::GetCoarseSteadyTimeMs(), std::memory_order_release);
  } else {
    thread_stat = CreateTlsStat();
  }
  ServiceStat* stat_map = thread_stat->stat_map_.load(std::memory_order_acquire);
  InstanceStat& instance_stat = (*stat_map)[instance_gauge.instance_id];
  if (instance_stat.service_key_ == nullptr) {
    instance_stat.service_key_ = new ServiceKey(instance_gauge.service_key_);
  }
  InstanceCodeStat& code_stat = instance_stat.ret_code_stat_[instance_gauge.call_ret_code];
  if (instance_gauge.call_ret_status == kCallRetOk) {
    code_stat.success_count_++;
    code_stat.success_delay_ += instance_gauge.call_daley;
  } else {
    code_stat.error_count_++;
    code_stat.error_delay_ += instance_gauge.call_daley;
  }
  thread_stat->access_time_.store(Time::kMaxTime, std::memory_order_release);
  return kReturnOk;
}

void MonitorStatReporter::CollectData(std::map<ServiceKey, ServiceStat>& report_data) {
  for (std::size_t i = 0; i < perpare_data_.size(); ++i) {
    ServiceStat& tls_data = *(perpare_data_[i]);
    for (ServiceStat::iterator it = tls_data.begin(); it != tls_data.end(); ++it) {
      ServiceStat& service_stat = report_data[*(it->second.service_key_)];
      InstanceStat& instance_stat = service_stat[it->first];
      for (std::map<int, InstanceCodeStat>::iterator code_it = it->second.ret_code_stat_.begin();
           code_it != it->second.ret_code_stat_.end(); ++code_it) {
        InstanceCodeStat& code_stat = instance_stat.ret_code_stat_[code_it->first];
        code_stat.success_count_ += code_it->second.success_count_;
        code_stat.success_delay_ += code_it->second.success_delay_;
        code_stat.error_count_ += code_it->second.error_count_;
        code_stat.error_delay_ += code_it->second.error_delay_;
      }
    }
    delete perpare_data_[i];
  }
  perpare_time_ = 0;
  perpare_data_.clear();
}

bool MonitorStatReporter::PerpareReport() {
  if (perpare_time_ == 0) {
    lock_.lock();
    for (std::set<TlsInstanceStat*>::iterator it = tls_stat_set_.begin(); it != tls_stat_set_.end();) {
      perpare_data_.push_back((*it)->stat_map_.load(std::memory_order_acquire));
      if ((*it)->active_.load(std::memory_order_acquire)) {  // 线程还活跃，则新建一个空map
        ServiceStat* new_stat_map = new ServiceStat();
        (*it)->stat_map_.store(new_stat_map, std::memory_order_release);
        ++it;
      } else {  // 线程已经退出，则删除线程局部数据
        (*it)->stat_map_ = nullptr;
        delete (*it);
        tls_stat_set_.erase(it++);
      }
    }
    lock_.unlock();
    // 先交换map，再获取时间。只有等待其他线程的时间都比这里的时间大，说明再也拿不到旧map
    perpare_time_ = Time::GetCoarseSteadyTimeMs();
  }
  bool perpared = true;
  lock_.lock();
  for (std::set<TlsInstanceStat*>::iterator it = tls_stat_set_.begin(); it != tls_stat_set_.end(); ++it) {
    if ((*it)->access_time_.load(std::memory_order_acquire) <= perpare_time_) {
      perpared = false;
      break;
    }
  }
  lock_.unlock();
  return perpared;
}

TlsInstanceStat* MonitorStatReporter::CreateTlsStat() {
  TlsInstanceStat* thread_stat = new TlsInstanceStat();
  pthread_setspecific(tls_key_, thread_stat);
  lock_.lock();
  tls_stat_set_.insert(thread_stat);
  lock_.unlock();
  return thread_stat;
}

void MonitorStatReporter::OnThreadExit(void* ptr) {
  if (ptr != nullptr) {
    TlsInstanceStat* thread_stat = static_cast<TlsInstanceStat*>(ptr);
    // 线程退出的时候设置线程局部数据active为false
    thread_stat->active_.store(false, std::memory_order_release);
  }
}

}  // namespace polaris
