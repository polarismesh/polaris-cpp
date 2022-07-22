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

#include "monitor/monitor_reporter.h"

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/wrappers.pb.h>
#include <stddef.h>
#include <stdlib.h>
#include <v1/code.pb.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "api/consumer_api.h"
#include "config/seed_server.h"
#include "context/context_impl.h"
#include "logger.h"
#include "model/location.h"
#include "monitor/api_stat_registry.h"
#include "monitor/service_record.h"
#include "plugin/circuit_breaker/chain.h"
#include "polaris/context.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "polaris/plugin.h"
#include "quota/quota_manager.h"
#include "reactor/reactor.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

ReportBase::ReportBase()
    : task_type_(kInvalidReportTask),
      reporter_(nullptr),
      report_interval_(0),
      timeout_check_flag_(false),
      instance_(nullptr),
      server_code_(kServerCodeReturnOk),
      call_begin_(0),
      grpc_client_(nullptr) {}

ReportBase::~ReportBase() {
  if (instance_ != nullptr) {  // 选择的实例未在上报完成后删除
    delete instance_;
    instance_ = nullptr;
  }
  if (grpc_client_ != nullptr) {
    delete grpc_client_;
    grpc_client_ = nullptr;
  }
}

void ReportBase::Init(ReportTaskType task_type, MonitorReporter* reporter, uint64_t report_interval) {
  task_type_ = task_type;
  reporter_ = reporter;
  report_interval_ = report_interval;
}

const char* ReportBase::GetCallPath() {
  switch (task_type_) {
    case kSdkConfigReport:
      return "/v1.GrpcAPI/CollectSDKConfiguration";
    case kSdkApiStatReport:
      return "/v1.GrpcAPI/CollectSDKAPIStatistics";
    case kServiceStatReport:
      return "/v1.GrpcAPI/CollectServiceStatistics";
    case kServiceCacheReport:
      return "/v1.GrpcAPI/CollectSDKCache";
    case kServiceCircuitReport:
      return "/v1.GrpcAPI/CollectCircuitBreak";
    case kServiceSetCircuitReport:
      return "/v1.GrpcAPI/CollectCircuitBreak";
    case kServiceRateLimitReport:
      return "/v1.GrpcAPI/CollectRateLimitRecord";
    case kServiceRouterStatReport:
      return "/v1.GrpcAPI/CollectRouteRecord";
    default:
      POLARIS_ASSERT(false);
      return "";
  }
}

const char* ReportBase::TaskTypeToStr() {
  switch (task_type_) {
    case kSdkConfigReport:
      return "sdk config";
    case kSdkApiStatReport:
      return "sdk api stat";
    case kServiceStatReport:
      return "service stat";
    case kServiceCacheReport:
      return "service cache";
    case kServiceCircuitReport:
      return "service circuit";
    case kServiceSetCircuitReport:
      return "service set circuit";
    case kServiceRateLimitReport:
      return "rate limit record";
    case kServiceRouterStatReport:
      return "service router stat";
    default:
      POLARIS_ASSERT(false);
      return "";
  }
}

bool ReportBase::PrepareGrpcClient() {
  if (!reporter_->GetInstance(this)) {
    return false;
  }
  POLARIS_ASSERT(timeout_check_flag_ == false);
  POLARIS_ASSERT(grpc_client_ == nullptr);
  grpc_client_ = new grpc::GrpcClient(reporter_->GetReactor());
  if (!grpc_client_->ConnectTo(instance_->GetHost(), instance_->GetPort()) ||
      !grpc_client_->WaitConnected(reporter_->GetConnectTimeout())) {
    POLARIS_STAT_LOG(LOG_ERROR, "connect to monitor server:%s:%d for report %s error", instance_->GetHost().c_str(),
                     instance_->GetPort(), TaskTypeToStr());
    server_code_ = kServerCodeConnectError;  // 连接超时
    reporter_->UpdateCallResult(this);
    return false;
  }
  grpc_client_->SubmitToReactor();
  return true;
}

///////////////////////////////////////////////////////////////////////////////

TimingFuncTask<UnaryReport>::Func UnaryReport::GetReportFunc() {
  switch (task_type_) {
    case kSdkConfigReport:
      return MonitorReporter::ReportSdkConfig;
    default:
      POLARIS_ASSERT(false);
      return nullptr;
  }
}

void UnaryReport::SetUpTimingReport(uint64_t interval) {
  Reactor& reactor = reporter_->GetReactor();
  reactor.AddTimingTask(new TimingFuncTask<UnaryReport>(this->GetReportFunc(), this, interval));
}

void UnaryReport::OnSuccess(::v1::StatResponse* response) {
  POLARIS_ASSERT(timeout_check_flag_);
  reporter_->GetReactor().CancelTimingTask(timeout_check_iter_);
  timeout_check_flag_ = false;

  POLARIS_ASSERT(response != nullptr);
  server_code_ = ToPolarisServerCode(response->code().value());
  if (server_code_ == kServerCodeReturnOk) {
    POLARIS_STAT_LOG(LOG_INFO, "send %s to monitor success", this->TaskTypeToStr());
  } else {
    POLARIS_STAT_LOG(LOG_WARN, "send %s to monitor failed with server error %d-%s", this->TaskTypeToStr(),
                     response->code().value(), response->info().value().c_str());
  }
  delete response;
  reporter_->UpdateCallResult(this);
  SetUpTimingReport(report_interval_);
}

void UnaryReport::OnFailure(const std::string& message) {
  POLARIS_ASSERT(timeout_check_flag_);
  reporter_->GetReactor().CancelTimingTask(timeout_check_iter_);
  timeout_check_flag_ = false;

  server_code_ = kServerCodeRpcError;
  reporter_->UpdateCallResult(this);
  POLARIS_STAT_LOG(LOG_WARN, "send %s to monitor failed with rpc error %s", this->TaskTypeToStr(), message.c_str());
  SetUpTimingReport(report_interval_);
}

void UnaryReport::TimeoutCheck(UnaryReport* unary_report) {
  POLARIS_STAT_LOG(LOG_WARN, "send %s to monitor timeout", unary_report->TaskTypeToStr());
  unary_report->timeout_check_flag_ = false;
  // 上报调用结果
  unary_report->server_code_ = kServerCodeRpcTimeout;
  unary_report->reporter_->UpdateCallResult(unary_report);

  // 设置下个周期的上报任务
  uint64_t delay = Time::GetCoarseSteadyTimeMs() - unary_report->call_begin_;
  uint64_t report_interval = unary_report->report_interval_ > delay ? unary_report->report_interval_ - delay : 1000;
  unary_report->SetUpTimingReport(report_interval);
}

///////////////////////////////////////////////////////////////////////////////
TimingFuncTask<StreamReport>::Func StreamReport::GetReportFunc() {
  switch (task_type_) {
    case kSdkApiStatReport:
      return MonitorReporter::ReportSdkApiStat;
    case kServiceStatReport:
      return MonitorReporter::ReportServiceStat;
    case kServiceCacheReport:
      return MonitorReporter::ReportServiceCache;
    case kServiceCircuitReport:
      return MonitorReporter::ReportCircuitStat;
    case kServiceSetCircuitReport:
      return MonitorReporter::ReportSetCircuitStat;
    case kServiceRateLimitReport:
      return MonitorReporter::ReportRateLimit;
    case kServiceRouterStatReport:
      return MonitorReporter::ReportServiceRouterStat;
    default:
      POLARIS_ASSERT(false);
      return nullptr;
  }
}

void StreamReport::SetUpTimingReport(uint64_t interval) {
  Reactor& reactor = reporter_->GetReactor();
  reactor.AddTimingTask(new TimingFuncTask<StreamReport>(this->GetReportFunc(), this, interval));
}

void StreamReport::OnReceiveMessage(::v1::StatResponse* response) {
  POLARIS_ASSERT(response != nullptr);
  if (response->code().value() == v1::ExecuteSuccess) {
    succ_count_++;
  } else {
    fail_count_++;
  }
  delete response;
}

void StreamReport::OnRemoteClose(const std::string& message) {
  POLARIS_ASSERT(timeout_check_flag_);
  reporter_->GetReactor().CancelTimingTask(timeout_check_iter_);
  timeout_check_flag_ = false;
  server_code_ = succ_count_ == request_count_ ? kServerCodeReturnOk : kServerCodeRpcError;
  POLARIS_STAT_LOG(LOG_INFO, "send %s to monitor request_count:%d, succ_count:%d, fail_count:%d, rpc message:%s",
                   this->TaskTypeToStr(), request_count_, succ_count_, fail_count_, message.c_str());
  succ_count_ = 0;
  fail_count_ = 0;
  reporter_->UpdateCallResult(this);
  SetUpTimingReport(report_interval_);
}

grpc::GrpcStream* StreamReport::PerpareStream() {
  if (!PrepareGrpcClient()) {
    return nullptr;
  }
  POLARIS_ASSERT(grpc_client_ != nullptr);
  grpc::GrpcStream* grpc_stream = grpc_client_->StartStream(this->GetCallPath(), *this);
  POLARIS_ASSERT(grpc_stream != nullptr);
  return grpc_stream;
}

void StreamReport::TimeoutCheck(StreamReport* stream_report) {
  POLARIS_STAT_LOG(LOG_WARN, "send %s to monitor timeout, request_count:%d, succ_count:%d, fail_count:%d",
                   stream_report->TaskTypeToStr(), stream_report->request_count_, stream_report->succ_count_,
                   stream_report->fail_count_);
  stream_report->timeout_check_flag_ = false;
  stream_report->succ_count_ = 0;
  stream_report->fail_count_ = 0;
  // 上报调用结果
  stream_report->server_code_ = kServerCodeRpcTimeout;
  stream_report->reporter_->UpdateCallResult(stream_report);

  // 设置下个周期的上报任务
  stream_report->SetUpTimingReport(stream_report->report_interval_ - stream_report->reporter_->GetMessageTimeout());
}

///////////////////////////////////////////////////////////////////////////////
MonitorReporter::MonitorReporter(Context* context)
    : Executor(context),
      sdk_token_(context->GetContextImpl()->GetSdkToken()),
      connect_timeout_(500),
      message_timeout_(2000) {}

MonitorReporter::~MonitorReporter() {}

void MonitorReporter::SetupWork() {  // 线程启动前先设置
  uint64_t report_interval = 5 * 60 * 1000;
  sdk_config_report_.Init(kSdkConfigReport, this, report_interval);
  sdk_config_report_.SetUpTimingReport(10 * 1000);  // SDK配置可以在启动后10s立刻上报

  report_interval = 60 * 1000;
  sdk_api_report_.Init(kSdkApiStatReport, this, report_interval);
  sdk_api_report_.SetUpTimingReport(report_interval);

  service_stat_report_.Init(kServiceStatReport, this, report_interval);
  service_stat_report_.SetUpTimingReport(report_interval);

  report_interval = 2 * 60 * 1000;
  service_cache_report_.Init(kServiceCacheReport, this, report_interval);
  service_cache_report_.SetUpTimingReport(report_interval);

  circuit_stat_report_.Init(kServiceCircuitReport, this, report_interval);
  circuit_stat_report_.SetUpTimingReport(report_interval);

  set_circuit_stat_report_.Init(kServiceSetCircuitReport, this, report_interval);
  set_circuit_stat_report_.SetUpTimingReport(report_interval);

  rate_limit_report_.Init(kServiceRateLimitReport, this, report_interval);
  rate_limit_report_.SetUpTimingReport(report_interval);

  report_interval = 5 * 60 * 1000;
  service_router_stat_report_.Init(kServiceRouterStatReport, this, report_interval);
  service_router_stat_report_.SetUpTimingReport(report_interval);
}

void MonitorReporter::BuildSdkConfig(v1::SDKConfig& sdk_config) {
  ContextImpl* context_impl = context_->GetContextImpl();
  const ContextConfig& context_config = context_impl->GetContextConfig();
  Location location;
  context_impl->GetClientLocation().GetLocation(location);
  sdk_config.mutable_token()->CopyFrom(sdk_token_);
  Time::Uint64ToTimestamp(context_config.take_effect_time_, sdk_config.mutable_take_effect_time());
  sdk_config.set_config(context_config.config_);
  sdk_config.set_location(location.ToString());
  sdk_config.set_client(sdk_token_.client());
  sdk_config.set_version(sdk_token_.version());
  Time::Uint64ToTimestamp(context_config.init_finish_time_, sdk_config.mutable_init_finish_time());
  Time::Uint64ToTimestamp(Time::GetSystemTimeMs(), sdk_config.mutable_report_time());
  POLARIS_STAT_LOG(LOG_INFO, "prepare report sdk config:%s token:%s location:%s client:%s version:%s",
                   context_config.config_.c_str(), sdk_token_.ShortDebugString().c_str(), sdk_config.location().c_str(),
                   sdk_config.client().c_str(), sdk_config.version().c_str());
}

void MonitorReporter::ReportSdkConfig(UnaryReport* unary_report) {
  MonitorReporter* reporter = unary_report->reporter_;
  v1::SDKConfig sdk_config;
  reporter->BuildSdkConfig(sdk_config);
  if (!unary_report->PrepareGrpcClient()) {  // 创建grpc client失败
    unary_report->SetUpTimingReport(unary_report->report_interval_);
    return;
  }
  unary_report->grpc_client_->SendRequest(sdk_config, unary_report->GetCallPath(),
                                          unary_report->reporter_->GetMessageTimeout(), *unary_report);
  unary_report->timeout_check_iter_ = reporter->reactor_.AddTimingTask(new TimingFuncTask<UnaryReport>(
      UnaryReport::TimeoutCheck, unary_report, unary_report->reporter_->GetMessageTimeout()));
  unary_report->timeout_check_flag_ = true;
}

void MonitorReporter::ReportSdkApiStat(StreamReport* stream_report) {
  MonitorReporter* reporter = stream_report->reporter_;
  ApiStatRegistry* api_stat_registry = reporter->context_->GetContextImpl()->GetApiStatRegistry();
  google::protobuf::RepeatedField<v1::SDKAPIStatistics> statistics;
  api_stat_registry->GetApiStatistics(statistics);
  if (statistics.empty()) {  // 这个周期没有数据
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  grpc::GrpcStream* grpc_stream = stream_report->PerpareStream();
  if (grpc_stream == nullptr) {
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  stream_report->request_count_ = statistics.size();
  for (int i = 0; i < statistics.size(); ++i) {
    grpc_stream->SendMessage(statistics[i], i + 1 == statistics.size());
  }
  // 根据请求数量增加超时时间
  uint64_t message_timeout = stream_report->reporter_->GetMessageTimeout(stream_report->request_count_);
  stream_report->timeout_check_iter_ = stream_report->reporter_->reactor_.AddTimingTask(
      new TimingFuncTask<StreamReport>(StreamReport::TimeoutCheck, stream_report, message_timeout));
  stream_report->timeout_check_flag_ = true;
}

// 处理服务实例下每个错误码的统计数据
static void AddInstancesCodeStat(google::protobuf::RepeatedField<v1::ServiceStatistics>& service_stat_list,
                                 const v1::ServiceStatisticsKey& stat_key, const std::string& instance_id,
                                 std::map<int, InstanceCodeStat>& instance_stat, InstanceCodeStat& instance_total,
                                 const v1::SDKToken& sdk_token) {
  for (std::map<int, InstanceCodeStat>::iterator it = instance_stat.begin(); it != instance_stat.end(); ++it) {
    InstanceCodeStat& code_stat = it->second;
    if (stat_key.has_instance_host() && code_stat.success_count_ > 0) {
      v1::ServiceStatistics* statistic = service_stat_list.Add();
      statistic->mutable_id()->set_value(std::to_string(Utils::GetNextSeqId()));
      v1::ServiceStatisticsKey* succ_key = statistic->mutable_key();
      succ_key->CopyFrom(stat_key);
      succ_key->set_res_code(it->first);
      succ_key->mutable_success()->set_value(true);
      v1::ServiceIndicator* succ_value = statistic->mutable_value();
      succ_value->mutable_total_request_per_minute()->set_value(code_stat.success_count_);
      succ_value->mutable_total_delay_per_minute()->set_value(code_stat.success_delay_);
      statistic->mutable_sdk_token()->CopyFrom(sdk_token);
    }
    if (stat_key.has_instance_host() && code_stat.error_count_ > 0) {
      v1::ServiceStatistics* statistic = service_stat_list.Add();
      statistic->mutable_id()->set_value(std::to_string(Utils::GetNextSeqId()));
      v1::ServiceStatisticsKey* error_key = statistic->mutable_key();
      error_key->CopyFrom(stat_key);
      error_key->set_res_code(it->first);
      error_key->mutable_success()->set_value(false);
      v1::ServiceIndicator* err_value = statistic->mutable_value();
      err_value->mutable_total_request_per_minute()->set_value(code_stat.error_count_);
      err_value->mutable_total_delay_per_minute()->set_value(code_stat.error_delay_);
      statistic->mutable_sdk_token()->CopyFrom(sdk_token);
    }
    POLARIS_STAT_LOG(LOG_INFO, "service stat: service[%s/%s], instance[%s:%s], ret_code[%d], %s",
                     stat_key.namespace_().value().c_str(), stat_key.service().value().c_str(), instance_id.c_str(),
                     stat_key.instance_host().value().c_str(), it->first, code_stat.ToString().c_str());
    instance_total.success_count_ += code_stat.success_count_;
    instance_total.success_delay_ += code_stat.success_delay_;
    instance_total.error_count_ += code_stat.error_count_;
    instance_total.error_delay_ += code_stat.error_delay_;
  }
}

// 处理服务下每个实例的统计数据
static void AddServiceInstancesStat(google::protobuf::RepeatedField<v1::ServiceStatistics>& report_data,
                                    v1::ServiceStatisticsKey& stat_key, ServiceInstances* service_instances,
                                    ServiceStat& service_stat, InstanceCodeStat& service_total,
                                    const v1::SDKToken& sdk_token) {
  for (ServiceStat::iterator it = service_stat.begin(); it != service_stat.end(); ++it) {
    InstanceCodeStat instance_total;
    stat_key.clear_instance_host();
    if (service_instances != nullptr) {  // 查找服务实例
      std::map<std::string, Instance*>& instances = service_instances->GetInstances();
      std::map<std::string, Instance*>::iterator instance_it = instances.find(it->first);
      if (instance_it != instances.end()) {
        stat_key.mutable_instance_host()->set_value(instance_it->second->GetHost() + ":" +
                                                    std::to_string(instance_it->second->GetPort()));
      }
    }
    AddInstancesCodeStat(report_data, stat_key, it->first, it->second.ret_code_stat_, instance_total, sdk_token);
    POLARIS_STAT_LOG(LOG_INFO, "service stat instance total: service[%s/%s], instance[%s:%s], %s",
                     stat_key.namespace_().value().c_str(), stat_key.service().value().c_str(), it->first.c_str(),
                     stat_key.instance_host().value().c_str(), instance_total.ToString().c_str());
    service_total.success_count_ += instance_total.success_count_;
    service_total.success_delay_ += instance_total.success_delay_;
    service_total.error_count_ += instance_total.error_count_;
    service_total.error_delay_ += instance_total.error_delay_;
  }
}

// 构建本上报周期所有服务统计的上报数据
void MonitorReporter::BuildServiceStat(std::map<ServiceKey, ServiceStat>& stat_data,
                                       google::protobuf::RepeatedField<v1::ServiceStatistics>& report_data) {
  LocalRegistry* local_registry = context_->GetLocalRegistry();
  ContextImpl* context_impl = context_->GetContextImpl();
  v1::ServiceStatisticsKey stat_key;
  stat_key.mutable_caller_host()->set_value(context_->GetContextImpl()->GetApiBindIp());
  for (std::map<ServiceKey, ServiceStat>::iterator it = stat_data.begin(); it != stat_data.end(); ++it) {
    stat_key.mutable_namespace_()->set_value(it->first.namespace_);
    stat_key.mutable_service()->set_value(it->first.name_);
    ServiceData* service_data = nullptr;
    context_impl->RcuEnter();
    local_registry->GetServiceDataWithRef(it->first, kServiceDataInstances, service_data);
    context_impl->RcuExit();
    ServiceInstances* service_instances = nullptr;
    if (service_data != nullptr) {
      service_instances = new ServiceInstances(service_data);
    }
    InstanceCodeStat service_total;
    AddServiceInstancesStat(report_data, stat_key, service_instances, it->second, service_total, sdk_token_);
    POLARIS_STAT_LOG(LOG_INFO, "service stat total: service[%s/%s], %s %s", it->first.namespace_.c_str(),
                     it->first.name_.c_str(), service_total.ToString().c_str(),
                     service_instances != nullptr ? "" : "service not found");
    if (service_instances != nullptr) {
      service_data->DecrementRef();
      delete service_instances;
    }
  }
}

void MonitorReporter::ReportServiceStat(StreamReport* stream_report) {
  MonitorReporter* reporter = stream_report->reporter_;
  StatReporter* stat_reporter = reporter->context_->GetContextImpl()->GetStatReporter();
  MonitorStatReporter* monitor_stat_reporter = reinterpret_cast<MonitorStatReporter*>(stat_reporter);
  POLARIS_ASSERT(monitor_stat_reporter != nullptr);
  if (!monitor_stat_reporter->PerpareReport()) {  // 到了上报时间但线程数据未交换，100ms后重试
    stream_report->SetUpTimingReport(100);
    return;
  }
  std::map<ServiceKey, ServiceStat> service_stat_map;
  monitor_stat_reporter->CollectData(service_stat_map);
  if (service_stat_map.empty()) {
    POLARIS_STAT_LOG(LOG_INFO, "no service stat to report this period");
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  google::protobuf::RepeatedField<v1::ServiceStatistics> service_stat;
  reporter->BuildServiceStat(service_stat_map, service_stat);
  grpc::GrpcStream* grpc_stream = stream_report->PerpareStream();
  if (grpc_stream == nullptr) {
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  stream_report->request_count_ = service_stat.size();
  for (int i = 0; i < service_stat.size(); ++i) {
    grpc_stream->SendMessage(service_stat[i], i + 1 == service_stat.size());
  }
  // 根据请求数量增加超时时间
  uint64_t message_timeout = stream_report->reporter_->GetMessageTimeout(stream_report->request_count_);
  stream_report->timeout_check_iter_ = stream_report->reporter_->reactor_.AddTimingTask(
      new TimingFuncTask<StreamReport>(StreamReport::TimeoutCheck, stream_report, message_timeout));
  stream_report->timeout_check_flag_ = true;
}

void MonitorReporter::ReportServiceCache(StreamReport* stream_report) {
  MonitorReporter* reporter = stream_report->reporter_;
  ServiceRecord* service_record = reporter->context_->GetContextImpl()->GetServiceRecord();
  std::map<ServiceKey, ::v1::ServiceInfo> service_cache;
  service_record->ReportServiceCache(service_cache);
  if (service_cache.empty()) {
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  grpc::GrpcStream* grpc_stream = stream_report->PerpareStream();
  if (grpc_stream == nullptr) {
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  stream_report->request_count_ = static_cast<int>(service_cache.size());
  int send_count = 0;
  for (std::map<ServiceKey, ::v1::ServiceInfo>::iterator it = service_cache.begin(); it != service_cache.end(); ++it) {
    it->second.mutable_sdk_token()->CopyFrom(reporter->sdk_token_);
    grpc_stream->SendMessage(it->second, ++send_count == stream_report->request_count_);
  }
  // 根据请求数量增加超时时间
  uint64_t message_timeout = stream_report->reporter_->GetMessageTimeout(stream_report->request_count_);
  stream_report->timeout_check_iter_ = stream_report->reporter_->reactor_.AddTimingTask(
      new TimingFuncTask<StreamReport>(StreamReport::TimeoutCheck, stream_report, message_timeout));
  stream_report->timeout_check_flag_ = true;
}

v1::StatusChange CircuitStatusToChange(CircuitBreakerStatus from, CircuitBreakerStatus to) {
  if (from == kCircuitBreakerClose) {
    if (to == kCircuitBreakerOpen) {
      return v1::StatusChangeCloseToOpen;
    }
  } else if (from == kCircuitBreakerOpen) {
    if (to == kCircuitBreakerHalfOpen) {
      return v1::StatusChangeOpenToHalfOpen;
    }
  } else if (from == kCircuitBreakerHalfOpen) {
    if (to == kCircuitBreakerOpen) {
      return v1::StatusChangeHalfOpenToOpen;
    } else if (to == kCircuitBreakerClose) {
      return v1::StatusChangeHalfOpenToClose;
    }
  }
  return v1::StatusChangeUnknown;
}

void BuildServiceCircuitStat(InstanceRecords& instance_records, ServiceData* service_data,
                             v1::ServiceCircuitbreak* service_circuit) {
  const ServiceKey& service_key = service_data->GetServiceKey();
  ServiceInstances service_instances(service_data);
  std::map<std::string, Instance*>& instances = service_instances.GetInstances();
  std::map<std::string, Instance*>::iterator instance_it;
  std::map<std::string, std::vector<CircuitChangeRecord*>>& circuit_change = instance_records.circuit_record_;
  std::map<std::string, std::vector<CircuitChangeRecord*>>::iterator circuit_it;
  for (circuit_it = circuit_change.begin(); circuit_it != circuit_change.end(); ++circuit_it) {
    if ((instance_it = instances.find(circuit_it->first)) != instances.end()) {
      v1::CircuitbreakHistory* history = service_circuit->add_instance_circuitbreak();
      history->set_ip(instance_it->second->GetHost());
      history->set_port(instance_it->second->GetPort());
      history->set_vpc_id(instance_it->second->GetVpcId());
      std::vector<CircuitChangeRecord*>& circuit_history = circuit_it->second;
      for (std::size_t i = 0; i < circuit_history.size(); ++i) {
        v1::CircuitbreakChange* change = history->add_changes();
        Time::Uint64ToTimestamp(circuit_history[i]->change_time_, change->mutable_time());
        change->set_change_seq(circuit_history[i]->change_seq_);
        change->set_change(CircuitStatusToChange(circuit_history[i]->from_, circuit_history[i]->to_));
        change->set_reason(circuit_history[i]->reason_);
        POLARIS_STAT_LOG(LOG_INFO,
                         "report circuit stat service[%s/%s] instance[%s] host[%s:%d:%s] time[%s] "
                         "seq[%d] from[%s] to[%s] reason[%s]",
                         service_key.namespace_.c_str(), service_key.name_.c_str(), circuit_it->first.c_str(),
                         instance_it->second->GetHost().c_str(), instance_it->second->GetPort(),
                         instance_it->second->GetVpcId().c_str(),
                         StringUtils::TimeToStr(change->time().seconds()).c_str(), circuit_history[i]->change_seq_,
                         CircuitBreakerStatusToStr(circuit_history[i]->from_),
                         CircuitBreakerStatusToStr(circuit_history[i]->to_), circuit_history[i]->reason_.c_str());
      }
    } else {
      POLARIS_STAT_LOG(LOG_WARN, "report circuit stat with service[%s/%s] instance[%s] not found",
                       service_key.namespace_.c_str(), service_key.name_.c_str(), circuit_it->first.c_str());
    }
  }
  service_data->DecrementRef();
  std::vector<RecoverAllRecord*>& recover_change = instance_records.recover_record_;
  for (std::size_t i = 0; i < recover_change.size(); ++i) {
    v1::RecoverAllChange* change = service_circuit->add_recover_all();
    Time::Uint64ToTimestamp(recover_change[i]->recover_time_, change->mutable_time());
    change->set_instance_info(recover_change[i]->cluster_info_);
    change->set_change(recover_change[i]->recover_status_ ? v1::RecoverAllStatusStart : v1::RecoverAllStatusEnd);
    POLARIS_STAT_LOG(
        LOG_INFO, "report circuit stat recover all service[%s/%s] %s at[%s] with info[%s]",
        service_key.namespace_.c_str(), service_key.name_.c_str(), recover_change[i]->recover_status_ ? "begin" : "end",
        StringUtils::TimeToStr(change->time().seconds()).c_str(), recover_change[i]->cluster_info_.c_str());
  }
}

void SplitToSetAndLabelStr(const std::string& set_label_str, std::string& set_str, std::string& label_str) {
  std::string::size_type split_idx = set_label_str.find("#");
  set_str = set_label_str.substr(0, split_idx);
  label_str = set_label_str.substr(split_idx + 1, set_label_str.size());
}

void SplitCircuitChangeRecordByRuleId(std::vector<CircuitChangeRecord*>& records,
                                      std::map<std::string, std::vector<CircuitChangeRecord*>>& ret_map) {
  for (size_t i = 0; i < records.size(); ++i) {
    ret_map[records[i]->circuit_breaker_conf_id_].push_back(records[i]);
  }
}

void BuildServiceSetCircuitStat(SetRecords& set_records, v1::ServiceCircuitbreak* service_circuit) {
  std::map<std::string, std::vector<CircuitChangeRecord*>>& circuit_change = set_records.circuit_record_;
  std::map<std::string, std::vector<CircuitChangeRecord*>>::iterator circuit_it;
  for (circuit_it = circuit_change.begin(); circuit_it != circuit_change.end(); ++circuit_it) {
    std::map<std::string, std::vector<CircuitChangeRecord*>> rule_map;
    SplitCircuitChangeRecordByRuleId(circuit_it->second, rule_map);

    std::map<std::string, std::vector<CircuitChangeRecord*>>::iterator it;
    for (it = rule_map.begin(); it != rule_map.end(); ++it) {
      v1::SubsetCbHistory* history = service_circuit->add_subset_circuitbreak();
      std::string set, label;
      SplitToSetAndLabelStr(circuit_it->first, set, label);
      history->set_subset(set);
      history->set_labels(label);
      history->set_ruleid(it->first);
      std::vector<CircuitChangeRecord*>& circuit_history = it->second;
      for (std::size_t i = 0; i < circuit_history.size(); ++i) {
        v1::CircuitbreakChange* change = history->add_changes();
        Time::Uint64ToTimestamp(circuit_history[i]->change_time_, change->mutable_time());
        change->set_change_seq(circuit_history[i]->change_seq_);
        change->set_change(CircuitStatusToChange(circuit_history[i]->from_, circuit_history[i]->to_));
        change->set_reason(circuit_history[i]->reason_);
      }
    }
  }
}

void MonitorReporter::BuildCircuitStat(std::map<ServiceKey, InstanceRecords>& circuit_stat,
                                       google::protobuf::RepeatedField<v1::ServiceCircuitbreak>& report_data) {
  LocalRegistry* local_registry = context_->GetLocalRegistry();
  ServiceData* service_data = nullptr;
  ContextImpl* context_impl = context_->GetContextImpl();
  for (std::map<ServiceKey, InstanceRecords>::iterator it = circuit_stat.begin(); it != circuit_stat.end(); ++it) {
    service_data = nullptr;
    context_impl->RcuEnter();
    local_registry->GetServiceDataWithRef(it->first, kServiceDataInstances, service_data);
    context_impl->RcuExit();
    if (service_data != nullptr) {
      v1::ServiceCircuitbreak* service_circuit = report_data.Add();
      service_circuit->set_id(std::to_string(Utils::GetNextSeqId()));
      service_circuit->set_namespace_(it->first.namespace_);
      service_circuit->set_service(it->first.name_);
      service_circuit->mutable_sdk_token()->CopyFrom(sdk_token_);
      BuildServiceCircuitStat(it->second, service_data, service_circuit);
    } else {
      POLARIS_STAT_LOG(LOG_WARN, "report circuit stat with service[%s/%s] not found", it->first.namespace_.c_str(),
                       it->first.name_.c_str());
    }
  }
}

void MonitorReporter::ReportCircuitStat(StreamReport* stream_report) {
  MonitorReporter* reporter = stream_report->reporter_;
  ServiceRecord* service_record = reporter->context_->GetContextImpl()->GetServiceRecord();
  std::map<ServiceKey, InstanceRecords> circuit_stat;
  service_record->ReportCircuitStat(circuit_stat);
  google::protobuf::RepeatedField<v1::ServiceCircuitbreak> report_data;
  reporter->BuildCircuitStat(circuit_stat, report_data);
  if (report_data.empty()) {
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  grpc::GrpcStream* grpc_stream = stream_report->PerpareStream();
  if (grpc_stream == nullptr) {
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  stream_report->request_count_ = report_data.size();
  for (int i = 0; i < report_data.size(); ++i) {
    grpc_stream->SendMessage(report_data[i], i + 1 == report_data.size());
  }
  // 根据请求数量增加超时时间
  uint64_t message_timeout = stream_report->reporter_->GetMessageTimeout(stream_report->request_count_);
  stream_report->timeout_check_iter_ = stream_report->reporter_->reactor_.AddTimingTask(
      new TimingFuncTask<StreamReport>(StreamReport::TimeoutCheck, stream_report, message_timeout));
  stream_report->timeout_check_flag_ = true;
}

void MonitorReporter::BuildSetCircuitStat(std::map<ServiceKey, SetRecords>& set_circuit_stat,
                                          google::protobuf::RepeatedField<v1::ServiceCircuitbreak>& report_data) {
  for (std::map<ServiceKey, SetRecords>::iterator it = set_circuit_stat.begin(); it != set_circuit_stat.end(); ++it) {
    v1::ServiceCircuitbreak* set_circuit = report_data.Add();
    set_circuit->set_id(std::to_string(Utils::GetNextSeqId()));
    set_circuit->set_namespace_(it->first.namespace_);
    set_circuit->set_service(it->first.name_);
    set_circuit->mutable_sdk_token()->CopyFrom(sdk_token_);
    BuildServiceSetCircuitStat(it->second, set_circuit);
  }
  return;
}

void MonitorReporter::ReportSetCircuitStat(StreamReport* stream_report) {
  MonitorReporter* reporter = stream_report->reporter_;
  ServiceRecord* service_record = reporter->context_->GetContextImpl()->GetServiceRecord();
  std::map<ServiceKey, SetRecords> set_circuit_stat;
  service_record->ReportSetCircuitStat(set_circuit_stat);
  google::protobuf::RepeatedField<v1::ServiceCircuitbreak> report_data;
  reporter->BuildSetCircuitStat(set_circuit_stat, report_data);
  if (report_data.empty()) {
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  grpc::GrpcStream* grpc_stream = stream_report->PerpareStream();
  if (grpc_stream == nullptr) {
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  stream_report->request_count_ = report_data.size();
  for (int i = 0; i < report_data.size(); ++i) {
    grpc_stream->SendMessage(report_data[i], i + 1 == report_data.size());
  }
  // 根据请求数量增加超时时间
  uint64_t message_timeout = stream_report->reporter_->GetMessageTimeout(stream_report->request_count_);
  stream_report->timeout_check_iter_ = stream_report->reporter_->reactor_.AddTimingTask(
      new TimingFuncTask<StreamReport>(StreamReport::TimeoutCheck, stream_report, message_timeout));
  stream_report->timeout_check_flag_ = true;
  return;
}

void MonitorReporter::ReportRateLimit(StreamReport* stream_report) {
  MonitorReporter* reporter = stream_report->reporter_;
  QuotaManager* quota_manager = reporter->context_->GetContextImpl()->GetQuotaManager();
  google::protobuf::RepeatedField<v1::RateLimitRecord> report_data;
  quota_manager->CollectRecord(report_data);
  if (report_data.empty()) {
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  grpc::GrpcStream* grpc_stream = stream_report->PerpareStream();
  if (grpc_stream == nullptr) {
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  stream_report->request_count_ = report_data.size();
  for (int i = 0; i < report_data.size(); ++i) {
    v1::RateLimitRecord& record = report_data[i];
    record.set_id(std::to_string(Utils::GetNextSeqId()));
    record.mutable_sdk_token()->CopyFrom(reporter->sdk_token_);
    POLARIS_STAT_LOG(LOG_INFO,
                     "report rate limit service[%s/%s] labels[%s] subset[%s] limit stats count[%d] "
                     "threshold changes count[%d]",
                     record.namespace_().c_str(), record.service().c_str(), record.labels().c_str(),
                     record.subset().c_str(), record.limit_stats_size(), record.threshold_changes_size());
    grpc_stream->SendMessage(record, i + 1 == report_data.size());
  }
  // 根据请求数量增加超时时间
  uint64_t message_timeout = stream_report->reporter_->GetMessageTimeout(stream_report->request_count_);
  stream_report->timeout_check_iter_ = stream_report->reporter_->reactor_.AddTimingTask(
      new TimingFuncTask<StreamReport>(StreamReport::TimeoutCheck, stream_report, message_timeout));
  stream_report->timeout_check_flag_ = true;
  return;
}

void MonitorReporter::ReportServiceRouterStat(StreamReport* stream_report) {
  MonitorReporter* reporter = stream_report->reporter_;
  google::protobuf::RepeatedField<v1::ServiceRouteRecord> report_data;
  std::vector<std::shared_ptr<ServiceContext>> all_service_contexts;
  reporter->context_->GetContextImpl()->GetAllServiceContext(all_service_contexts);
  ServiceKey service_key;
  std::map<std::string, RouterStatData*> stat_data;
  std::map<std::string, RouterStatData*>::iterator stat_it;
  std::map<std::string, uint32_t>::iterator result_it;
  for (std::size_t i = 0; i < all_service_contexts.size(); ++i) {
    all_service_contexts[i]->GetServiceRouterChain()->CollectStat(service_key, stat_data);
    if (stat_data.empty()) {
      continue;
    }
    v1::ServiceRouteRecord* service_route_record = report_data.Add();
    service_route_record->set_namespace_(service_key.namespace_);
    service_route_record->set_service(service_key.name_);
    for (stat_it = stat_data.begin(); stat_it != stat_data.end(); ++stat_it) {
      RouterStatData* route_stat = stat_it->second;
      v1::RouteRecord* record = service_route_record->add_records();
      record->Swap(&route_stat->record_);
      POLARIS_STAT_LOG(LOG_INFO, "service router stat service[%s/%s] record [%s]", service_key.namespace_.c_str(),
                       service_key.name_.c_str(), record->ShortDebugString().c_str());
      delete route_stat;
    }
    stat_data.clear();
  }
  all_service_contexts.clear();

  if (report_data.empty()) {
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  grpc::GrpcStream* grpc_stream = stream_report->PerpareStream();
  if (grpc_stream == nullptr) {
    stream_report->SetUpTimingReport(stream_report->report_interval_);
    return;
  }
  stream_report->request_count_ = report_data.size();
  for (int i = 0; i < report_data.size(); ++i) {
    v1::ServiceRouteRecord& record = report_data[i];
    record.set_id(std::to_string(Utils::GetNextSeqId()));
    record.mutable_sdk_token()->CopyFrom(reporter->sdk_token_);
    Time::Uint64ToTimestamp(Time::GetSystemTimeMs(), record.mutable_time());
    grpc_stream->SendMessage(record, i + 1 == report_data.size());
  }
  // 根据请求数量增加超时时间
  uint64_t message_timeout = stream_report->reporter_->GetMessageTimeout(stream_report->request_count_);
  stream_report->timeout_check_iter_ = stream_report->reporter_->reactor_.AddTimingTask(
      new TimingFuncTask<StreamReport>(StreamReport::TimeoutCheck, stream_report, message_timeout));
  stream_report->timeout_check_flag_ = true;
  return;
}

uint64_t MonitorReporter::GetMessageTimeout(int request_count) {
  constexpr int kTimeoutBatchSize = 10;
  constexpr uint64_t kMaxMessageTimeout = 10 * 1000;  // 10s
  uint64_t message_timeout = message_timeout_ * (1 + request_count / kTimeoutBatchSize);
  if (message_timeout > kMaxMessageTimeout) {
    return kMaxMessageTimeout;
  }
  return message_timeout;
}

bool MonitorReporter::GetInstance(ReportBase* report_data) {
  POLARIS_ASSERT(report_data != nullptr);
  POLARIS_ASSERT(report_data->instance_ == nullptr);
  const PolarisCluster& monitor_cluster = context_->GetContextImpl()->GetMonitorService();
  Criteria criteria;
  criteria.ignore_half_open_ = (rand() % 10 != 0);  // 10分之一的概率选择半开节点
  ReturnCode ret_code = ConsumerApiImpl::GetSystemServer(context_, monitor_cluster.service_, criteria,
                                                         report_data->instance_, message_timeout_);
  if (ret_code == kReturnOk) {
    POLARIS_ASSERT(report_data->instance_ != nullptr);
    POLARIS_STAT_LOG(LOG_INFO, "get monitor server:%s:%d for %s", report_data->instance_->GetHost().c_str(),
                     report_data->instance_->GetPort(), report_data->TaskTypeToStr());
    report_data->call_begin_ = Time::GetCoarseSteadyTimeMs();
    return true;
  }
  POLARIS_STAT_LOG(LOG_ERROR, "get monitor server for %s with error:%s", report_data->TaskTypeToStr(),
                   ReturnCodeToMsg(ret_code).c_str());
  POLARIS_ASSERT(report_data->instance_ == nullptr);
  return false;
}

void MonitorReporter::UpdateCallResult(ReportBase* report_data) {
  POLARIS_ASSERT(report_data->instance_ != nullptr);
  const ServiceKey& service = context_->GetContextImpl()->GetMonitorService().service_;
  CallRetStatus status = kCallRetOk;
  if (kServerCodeConnectError <= report_data->server_code_ && report_data->server_code_ <= kServerCodeInvalidResponse) {
    status = kCallRetError;
  }
  uint64_t delay = Time::GetCoarseSteadyTimeMs() - report_data->call_begin_;
  ConsumerApiImpl::UpdateServerResult(context_, service, *report_data->instance_, report_data->server_code_, status,
                                      delay);
  delete report_data->instance_;
  report_data->instance_ = nullptr;
  POLARIS_ASSERT(report_data->grpc_client_ != nullptr);
  // 由于本方法在grpc stream的callback中调用，为了防止stream释放自身，需要异步释放grpc client
  report_data->grpc_client_->Close();
  reactor_.SubmitTask(new DeferDeleteTask<grpc::GrpcClient>(report_data->grpc_client_));
  report_data->grpc_client_ = nullptr;
}

}  // namespace polaris
