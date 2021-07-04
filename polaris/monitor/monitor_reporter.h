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

#ifndef POLARIS_CPP_POLARIS_MONITOR_MONITOR_REPORTER_H_
#define POLARIS_CPP_POLARIS_MONITOR_MONITOR_REPORTER_H_

#include <stdint.h>

#include <v1/request.pb.h>
#include <v1/response.pb.h>
#include <map>
#include <string>

#include "engine/executor.h"
#include "grpc/grpc_client.h"
#include "grpc/status.h"
#include "model/return_code.h"
#include "plugin/stat_reporter/stat_reporter.h"
#include "reactor/task.h"

namespace polaris {

class Context;
class Instance;
class MonitorReporter;
struct InstanceRecords;
struct ServiceKey;
struct SetRecords;

// 定义上报任务类型
enum ReportTaskType {
  kInvalidReportTask,
  kSdkConfigReport,
  kSdkApiStatReport,
  kServiceStatReport,
  kServiceCacheReport,
  kServiceCircuitReport,
  kServiceSetCircuitReport,
  kServiceRateLimitReport,
  kServiceRouterStatReport,
};

class MonitorReporter;
class ReportBase {
public:
  ReportBase();
  virtual ~ReportBase();

  void Init(ReportTaskType task_type, MonitorReporter* reporter, uint64_t report_interval);

  // 通过上报任务类型获取对应的RPC路径
  const char* GetCallPath();
  // 通过上报任务类型获取类型字符串用于日志输出
  const char* TaskTypeToStr();

  // 选择monitor节点，发起连接，并创建grpc client
  bool PrepareGrpcClient();

protected:
  friend class MonitorReporter;
  // 以下5项由Init函数设置
  ReportTaskType task_type_;
  MonitorReporter* reporter_;
  uint64_t report_interval_;

  // 记录超时检查任务
  bool timeout_check_flag_;
  TimingTaskIter timeout_check_iter_;

  // 记录上报调用选择的Monitor，包含开始时间及结果用于调用完成上报
  Instance* instance_;
  PolarisServerCode server_code_;
  uint64_t call_begin_;
  // 选择instance上创建的grpc client，在上报的时候删除
  grpc::GrpcClient* grpc_client_;
};

// Unary 模式的上报，目前只有SDK Config使用
class UnaryReport : public ReportBase, public grpc::RequestCallback<v1::StatResponse> {
public:
  UnaryReport() {}
  virtual ~UnaryReport() {}

  // 通过上报类型获取上报函数
  TimingFuncTask<UnaryReport>::Func GetReportFunc();

  // 设置定时上报任务
  void SetUpTimingReport(uint64_t interval);

  // RPC成功回调，取消超时检查，提交调用结果
  virtual void OnSuccess(::v1::StatResponse* response);

  // RPC失败回调，取消超时检查，提交调用结果
  virtual void OnFailure(grpc::GrpcStatusCode status, const std::string& message);

private:
  friend class MonitorReporter;
  static void TimeoutCheck(UnaryReport* unary_report);
};

class StreamReport : public ReportBase, public grpc::StreamCallback<v1::StatResponse> {
public:
  StreamReport() : request_count_(0), succ_count_(0), fail_count_(0) {}
  virtual ~StreamReport() {}

  // 通过上报类型获取上报函数
  TimingFuncTask<StreamReport>::Func GetReportFunc();

  // 设置定时上报任务
  void SetUpTimingReport(uint64_t interval);

  virtual void OnReceiveMessage(::v1::StatResponse* response);

  virtual void OnRemoteClose(grpc::GrpcStatusCode status, const std::string& message);

  grpc::GrpcStream* PerpareStream();

private:
  friend class MonitorReporter;
  static void TimeoutCheck(StreamReport* stream_report);

private:
  int request_count_;
  int succ_count_;
  int fail_count_;
};

class MonitorReporter : public Executor {
public:
  explicit MonitorReporter(Context* context);
  virtual ~MonitorReporter();

  virtual const char* GetName() { return "monitor_report"; }

  virtual void SetupWork();

  uint64_t GetConnectTimeout() { return connect_timeout_; }

  uint64_t GetMessageTimeout() { return message_timeout_; }

  bool GetInstance(ReportBase* report_data);

  void UpdateCallResult(ReportBase* report_data);

  static void ReportSdkConfig(UnaryReport* unary_report);

  static void ReportSdkApiStat(StreamReport* stream_report);

  static void ReportServiceStat(StreamReport* stream_report);

  static void ReportServiceCache(StreamReport* stream_report);

  static void ReportCircuitStat(StreamReport* stream_report);

  static void ReportSetCircuitStat(StreamReport* stream_report);

  static void ReportRateLimit(StreamReport* stream_report);

  static void ReportServiceRouterStat(StreamReport* stream_report);

  // 从服务统计数据构建服务统计上报PB
  void BuildServiceStat(std::map<ServiceKey, ServiceStat>& stat_data,
                        google::protobuf::RepeatedField<v1::ServiceStatistics>& report_data);

  // 构建set熔断上报PB
  void BuildSetCircuitStat(std::map<ServiceKey, SetRecords>& circuit_stat,
                           google::protobuf::RepeatedField<v1::ServiceCircuitbreak>& report_data);

private:
  void BuildSdkConfig(v1::SDKConfig& sdk_config);

  void BuildCircuitStat(std::map<ServiceKey, InstanceRecords>& circuit_stat,
                        google::protobuf::RepeatedField<v1::ServiceCircuitbreak>& report_data);

private:
  const v1::SDKToken& sdk_token_;
  uint64_t connect_timeout_;
  uint64_t message_timeout_;
  UnaryReport sdk_config_report_;
  StreamReport sdk_api_report_;
  StreamReport service_stat_report_;
  StreamReport service_cache_report_;
  StreamReport circuit_stat_report_;
  StreamReport set_circuit_stat_report_;
  StreamReport rate_limit_report_;
  StreamReport service_router_stat_report_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MONITOR_MONITOR_REPORTER_H_
