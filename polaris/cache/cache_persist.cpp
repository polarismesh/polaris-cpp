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

#include "cache_persist.h"

#include <dirent.h>
#include <errno.h>
#include <google/protobuf/stubs/status.h>
#include <google/protobuf/util/json_util.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <v1/model.pb.h>

#include <fstream>
#include <iterator>

#include "cache/persist_task.h"
#include "logger.h"
#include "model/constants.h"
#include "model/model_impl.h"
#include "polaris/config.h"
#include "reactor/reactor.h"
#include "utils/file_utils.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

CachePersistConfig::CachePersistConfig()
    : available_time_(0), upgrade_wait_time_(0), max_write_retry_(0), retry_interval_(0) {}

bool CachePersistConfig::Init(Config* config) {
  // 持久化目录
  static const char kPersistDirKey[]     = "persistDir";
  static const char kPersistDirDefault[] = "$HOME/polaris/backup/";
  persist_dir_ =
      FileUtils::ExpandPath(config->GetStringOrDefault(kPersistDirKey, kPersistDirDefault));
  if (persist_dir_.at(persist_dir_.size() - 1) != '/') {
    persist_dir_.append("/");
  }

  // 持久化数据可用时间
  static const char kAvailableTimeKey[]       = "availableTime";
  static const uint64_t kAvailableTimeDefault = 60 * 1000;
  available_time_ = config->GetMsOrDefault(kAvailableTimeKey, kAvailableTimeDefault);

  // 过期持久化数据升级时间
  static const char kUpgradeWaitTimeKey[]       = "upgradeWaitTime";
  static const uint64_t kUpgradeWaitTimeDefault = 2 * 1000;
  upgrade_wait_time_ = config->GetMsOrDefault(kUpgradeWaitTimeKey, kUpgradeWaitTimeDefault);

  // 持久化失败时重试次数
  static const char kMaxWriteRetryKey[]  = "persistMaxWriteRetry";
  static const int kMaxWriteRetryDefault = 5;
  max_write_retry_ = config->GetIntOrDefault(kMaxWriteRetryKey, kMaxWriteRetryDefault);
  if (max_write_retry_ <= 0) {
    POLARIS_LOG(LOG_ERROR, "%s must greater than 0, %d is invalid", kMaxWriteRetryKey,
                max_write_retry_);
    return false;
  }

  // 持久化失败时重试间隔
  static const char kRetryIntervalKey[]       = "persistRetryInterval";
  static const uint64_t kRetryIntervalDefault = 1000;
  retry_interval_ = config->GetMsOrDefault(kRetryIntervalKey, kRetryIntervalDefault);
  if (retry_interval_ <= 0) {
    POLARIS_LOG(LOG_ERROR, "%s must greater than 0, %" PRIu64 " is invalid", kRetryIntervalKey,
                retry_interval_);
    return false;
  }
  POLARIS_LOG(LOG_INFO, "cache persist config [%s:%s, %s:%d, %s:%" PRIu64 "]", kPersistDirKey,
              persist_dir_.c_str(), kMaxWriteRetryKey, max_write_retry_, kRetryIntervalKey,
              retry_interval_);
  return true;
}

CachePersist::CachePersist(Reactor& reactor) : reactor_(reactor) {}

ReturnCode CachePersist::Init(Config* config) {
  return persist_config_.Init(config) ? kReturnOk : kReturnInvalidConfig;
}

Location* CachePersist::LoadLocation() {
  if (!FileUtils::FileExists(persist_config_.GetPersistDir())) {
    if (!FileUtils::CreatePath(persist_config_.GetPersistDir())) {
      POLARIS_LOG(LOG_ERROR, "create persist dir[%s] failed, errno:%d",
                  persist_config_.GetPersistDir().c_str(), errno);
    }
    return NULL;  // 文件夹刚创建里面没数据直接退出
  }
  // format: location.json
  std::string full_file_name = persist_config_.GetPersistDir() + "location.json";
  if (!FileUtils::FileExists(full_file_name)) {
    return NULL;
  }
  std::ifstream input_file(full_file_name.c_str());
  if (input_file.bad()) {
    input_file.close();
    POLARIS_LOG(LOG_ERROR, "read location from file[%s] error, skip it", full_file_name.c_str());
    return NULL;
  }
  std::string data((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());
  input_file.close();
  // 解析数据
  v1::Location pb_location;
  google::protobuf::util::Status status =
      google::protobuf::util::JsonStringToMessage(data, &pb_location);
  if (!status.ok()) {
    POLARIS_LOG(LOG_ERROR, "create location from json[%s] error: %s", data.c_str(),
                status.error_message().data());
    return NULL;
  }
  Location* location = new Location();
  location->region   = pb_location.region().value();
  location->zone     = pb_location.zone().value();
  location->campus   = pb_location.campus().value();
  return location;
}

ServiceData* CachePersist::LoadServiceData(const ServiceKey& service_key,
                                           ServiceDataType data_type) {
  std::string file_name      = BuildFileName(service_key, data_type);
  std::string full_file_name = persist_config_.GetPersistDir() + file_name;

  uint64_t sync_time = 0;
  if (!FileUtils::GetModifiedTime(full_file_name, &sync_time)) {
    return NULL;
  }

  POLARIS_LOG(LOG_DEBUG, "prepare loading service data from file[%s]", full_file_name.c_str());
  std::ifstream input_file(full_file_name.c_str());
  if (input_file.bad()) {
    input_file.close();
    POLARIS_LOG(LOG_ERROR, "read service data file[%s] error, skip it", full_file_name.c_str());
    return NULL;
  }
  std::string data((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());
  input_file.close();
  uint64_t current_time   = Time::GetCurrentTimeMs();
  uint64_t available_time = current_time;
  // 如果磁盘缓存已经不在可用时间范围内，则需等待一段时间后从服务器同步失败则升级立即使用
  if (sync_time + persist_config_.GetAvailableTime() < available_time) {
    available_time += persist_config_.GetUpgradeWaitTime();
  }
  ServiceData* service_data = ServiceData::CreateFromJson(data, kDataInitFromDisk, available_time);
  if (service_data == NULL) {
    POLARIS_LOG(LOG_ERROR, "load service data for [%s/%s] with content[%s] error, skip it",
                service_key.namespace_.c_str(), service_key.namespace_.c_str(), data.c_str());
    return NULL;
  }
  // 校验数据
  if (service_data->GetServiceKey().namespace_ != service_key.namespace_ ||
      service_data->GetServiceKey().name_ != service_key.name_ ||
      service_data->GetDataType() != data_type) {
    POLARIS_LOG(LOG_ERROR, "service data not match file[%s], skip it", full_file_name.c_str());
    service_data->DecrementRef();
    return NULL;
  }
  POLARIS_LOG(LOG_INFO, "load %s from disk for service[%s/%s] succ, available after %" PRIu64 "s",
              DataTypeToStr(data_type), service_key.namespace_.c_str(), service_key.name_.c_str(),
              available_time - current_time);
  return service_data;
}

void CachePersist::PersistServiceData(const ServiceKey& service_key, ServiceDataType data_type,
                                      const std::string& data) {
  PersistTask* persist_task =
      new PersistTask(persist_config_.GetPersistDir() + BuildFileName(service_key, data_type), data,
                      persist_config_.GetMaxWriteRetry(), persist_config_.GetRetryInterval());
  reactor_.SubmitTask(persist_task);
}

void CachePersist::UpdateSyncTime(const ServiceKey& service_key, ServiceDataType data_type) {
  reactor_.SubmitTask(new PersistRefreshTimeTask(persist_config_.GetPersistDir() +
                                                 BuildFileName(service_key, data_type)));
}

void CachePersist::PersistLocation(const Location& location) {
  v1::Location pb_location;
  pb_location.mutable_region()->set_value(location.region);
  pb_location.mutable_zone()->set_value(location.zone);
  pb_location.mutable_campus()->set_value(location.campus);
  std::string json_content;
  google::protobuf::util::MessageToJsonString(pb_location, &json_content);
  PersistTask* persist_task =
      new PersistTask(persist_config_.GetPersistDir() + "/location.json", json_content,
                      persist_config_.GetMaxWriteRetry(), persist_config_.GetRetryInterval());
  reactor_.SubmitTask(persist_task);
}

std::string CachePersist::BuildFileName(const ServiceKey& service_key, ServiceDataType data_type) {
  std::string suffix = "#";
  if (data_type == kServiceDataInstances) {
    suffix.append(constants::kBackupFileInstanceSuffix);
  } else if (data_type == kServiceDataRouteRule) {
    suffix.append(constants::kBackupFileRoutingSuffix);
  } else if (data_type == kServiceDataRateLimit) {
    suffix.append(constants::kBackupFileRateLimitSuffix);
  } else if (data_type == kCircuitBreakerConfig) {
    suffix.append(constants::kBackupFileCircuitBreakerSuffix);
  } else {
    POLARIS_ASSERT(false);
  }
  // format: svc#[service namespace]#[service name]#[data type].json
  return "svc#" + Utils::UrlEncode(service_key.namespace_) + "#" +
         Utils::UrlEncode(service_key.name_) + suffix + ".json";
}

}  // namespace polaris
