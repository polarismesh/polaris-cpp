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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_STAT_REPORTER_STAT_REPORTER_H_
#define POLARIS_CPP_POLARIS_PLUGIN_STAT_REPORTER_STAT_REPORTER_H_

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "polaris/defs.h"
#include "polaris/plugin.h"
#include "utils/time_clock.h"

namespace polaris {

class Config;
class Context;
struct InstanceCodeStat {
  InstanceCodeStat() : success_count_(0), error_count_(0), success_delay_(0), error_delay_(0) {}
  uint32_t success_count_;
  uint32_t error_count_;
  uint64_t success_delay_;
  uint64_t error_delay_;

  std::string ToString();
};

struct InstanceStat {
  InstanceStat() : service_key_(nullptr) {}

  ~InstanceStat() {
    if (service_key_ != nullptr) {
      delete service_key_;
      service_key_ = nullptr;
    }
  }

  // 根据服务实例RPC返回码分开统计
  std::map<int, InstanceCodeStat> ret_code_stat_;
  ServiceKey* service_key_;
};

// 服务统计：每个服务实例统计自己的数据
typedef std::unordered_map<std::string, InstanceStat> ServiceStat;

struct TlsInstanceStat {
  TlsInstanceStat() : stat_map_(new ServiceStat()), access_time_(Time::GetCoarseSteadyTimeMs()), active_(true) {}

  ~TlsInstanceStat() {
    if (stat_map_ != nullptr) {
      delete stat_map_;
      stat_map_ = nullptr;
    }
  }

  std::atomic<ServiceStat*> stat_map_;
  std::atomic<uint64_t> access_time_;
  std::atomic<bool> active_;
};

class MonitorStatReporter : public StatReporter {
 public:
  MonitorStatReporter();

  virtual ~MonitorStatReporter();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual ReturnCode ReportStat(const InstanceGauge& instance_gauge);

  // 上报线程调用此方法，创建空的ServiceStat与线程使用中的进行交换
  // 返回是否交换完毕
  bool PerpareReport();

  // 上报线程调用PerpareReport成功后，调用此方法获取所有线程的数据
  void CollectData(std::map<ServiceKey, ServiceStat>& report_data);

 private:
  // 新线程创建线程局部存储
  TlsInstanceStat* CreateTlsStat();

  static void OnThreadExit(void* ptr);

 private:
  Context* context_;
  uint64_t report_interval_;

  std::mutex lock_;
  pthread_key_t tls_key_;
  std::set<TlsInstanceStat*> tls_stat_set_;

  uint64_t perpare_time_;
  std::vector<ServiceStat*> perpare_data_;  // 从线程中获取的准备上报的数据
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_STAT_REPORTER_STAT_REPORTER_H_
