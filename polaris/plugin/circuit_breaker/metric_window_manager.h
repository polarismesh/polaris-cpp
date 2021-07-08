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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_METRIC_WINDOW_MANAGER_H_
#define POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_METRIC_WINDOW_MANAGER_H_

#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "grpc/client.h"
#include "model/model_impl.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "v1/circuitbreaker.pb.h"
#include "v1/metric.pb.h"

namespace polaris {

class CircuitBreakSetChainData;
class CircuitBreakerExecutor;
class Context;
struct InstanceGauge;
template <typename Key, typename Value>
class RcuMap;

enum StatisticalStatus {
  Success = 0,
  Err,
  SpecificErr,
  Slow,
};

struct MetricReqStatus {
  StatisticalStatus status;
  std::string key;
};

struct CbMetricBucket {
public:
  void AddCount(const MetricReqStatus& status);
  ~CbMetricBucket();

  sync::Atomic<uint64_t> metric_total_count_;
  sync::Atomic<uint64_t> metric_err_count_;
  sync::Atomic<uint64_t> metric_slow_count_;
  std::map<std::string, sync::Atomic<uint64_t>*> specific_errs_count_;
};

class CircuitBreakSetChainData;

class MetricWindow : public ServiceBase {
public:
  MetricWindow(Context* context, const ServiceKey& service_key, SubSetInfo* set_info,
               Labels* labels, const v1::DestinationSet*& dst_set_conf, const std::string& cb_id,
               CircuitBreakSetChainData* chain_data);

  ~MetricWindow();

  ReturnCode Init(CircuitBreakerExecutor* executor, const std::string& version);

  std::string& GetVersion() { return version_; }

  ReturnCode AddCount(const InstanceGauge& gauge);

  static void TimingMetricReport(MetricWindow* window);

  static void TimingMetricQuery(MetricWindow* window);

  ReturnCode MetricQuery();

  ReturnCode MetricReportWithCallBack(void* callback);

  static void AsyncInit(MetricWindow* metric_window);

  v1::MetricInitRequest* AssembleInitReq();
  v1::MetricRequest* AssembleReportReq();

  void MarkDeleted() { is_delete_ = true; }

  bool IsDeleted() { return is_delete_.Load(); }

  void InitCallback(v1::MetricResponse* response);

  void QueryCallback(ReturnCode ret_code, v1::MetricResponse* response);

  std::string GetWindowKey();

private:
  ReturnCode InitBucket();
  ReturnCode InitErrorConf();
  ReturnCode InitSlowConf();

private:
  Context* context_;
  const ServiceKey& service_key_;
  v1::DestinationSet* dst_set_conf_;
  v1::DestinationSet dst_set_conf_obj_;
  // 规则ID
  std::string cb_conf_id_;

  SubSetInfo sub_set_info_;
  Labels labels_info_;

  std::vector<CbMetricBucket*> metric_buckets_;
  int metric_buckets_size_;

  // 统计周期
  uint64_t metric_window_;
  // 精度
  uint64_t metric_precision_;
  // bucket 长度
  uint64_t bucket_duration_;

  bool enable_err_rate_;
  bool enable_slow_rate_;
  uint64_t slow_rate_at_;

  std::map<std::string, std::set<int64_t> > specific_errors_;

  CircuitBreakerExecutor* executor_;
  CircuitBreakSetChainData* chain_data_;

  std::vector<v1::MetricDimension> metric_dims_;

  std::string version_;

  sync::Atomic<bool> is_delete_;

  sync::Atomic<uint64_t> cnt_;
  sync::Atomic<uint64_t> report_cnt_;

  uint64_t report_interval_;
  uint64_t query_interval_;

  int64_t send_req_timestamp_;
  int64_t time_diff_;

  v1::MetricKey metric_key_;
};

struct WindowInfo {
  std::string sub_set;
  std::string label;
  int64_t metric_window;
  int64_t metric_precision;
  const v1::DestinationSet* dst_conf_;
  std::vector<v1::MetricDimension> metric_dims;
  std::string cb_id;
};

class MetricWindowManager {
public:
  MetricWindowManager(Context* context, CircuitBreakerExecutor* executor);

  ~MetricWindowManager();

  MetricWindow* GetWindow(SubSetInfo& subset, Labels& labels);

  MetricWindow* UpdateWindow(const ServiceKey& service_key, SubSetInfo& subset, Labels& labels,
                             const std::string& version, const v1::DestinationSet* dst_set_conf,
                             const std::string& cb_id, CircuitBreakSetChainData* chain_data);

  void WindowGc();  // 定期已经删除的窗口

private:
  sync::Mutex update_lock_;
  RcuMap<std::string, MetricWindow>* windows_;

  CircuitBreakerExecutor* executor_;
  Context* context_;
};

class MetricInitCallBack : public grpc::RpcCallback<v1::MetricResponse> {
public:
  explicit MetricInitCallBack(MetricWindow* window);

  virtual ~MetricInitCallBack();

  virtual void OnSuccess(v1::MetricResponse* response);

  virtual void OnError(ReturnCode ret_code);

private:
  MetricWindow* window_;
};

class MetricReportCallBack : public grpc::RpcCallback<v1::MetricResponse> {
public:
  explicit MetricReportCallBack(MetricWindow* window, v1::MetricRequest& req);

  virtual ~MetricReportCallBack();

  virtual void OnSuccess(v1::MetricResponse* response);

  virtual void OnError(ReturnCode ret_code);

  const v1::MetricRequest& GetRequest() { return request_; }

private:
  MetricWindow* window_;
  v1::MetricRequest request_;
  int try_times_;
};

class MetricQueryCallback : public grpc::RpcCallback<v1::MetricResponse> {
public:
  explicit MetricQueryCallback(MetricWindow* window);

  ~MetricQueryCallback();

  virtual void OnSuccess(v1::MetricResponse* response);

  virtual void OnError(ReturnCode ret_code);

private:
  MetricWindow* window_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_CIRCUIT_BREAKER_METRIC_WINDOW_MANAGER_H_
