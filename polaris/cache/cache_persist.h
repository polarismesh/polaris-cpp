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

#ifndef POLARIS_CPP_POLARIS_CACHE_CACHE_PERSIST_H_
#define POLARIS_CPP_POLARIS_CACHE_CACHE_PERSIST_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "model/location.h"
#include "polaris/defs.h"
#include "polaris/model.h"
#include "reactor/task.h"

namespace polaris {

class Config;
class Reactor;

// 缓存持久化配置
class CachePersistConfig {
 public:
  CachePersistConfig();
  ~CachePersistConfig() {}

  bool Init(Config* config);

  const std::string& GetPersistDir() const { return persist_dir_; }

  int GetMaxWriteRetry() const { return max_write_retry_; }

  uint64_t GetRetryInterval() const { return retry_interval_; }

  uint64_t GetAvailableTime() const { return available_time_; }

  uint64_t GetUpgradeWaitTime() const { return upgrade_wait_time_; }

 private:
  std::string persist_dir_;     // 持久化目录
  uint64_t available_time_;     // 持久化数据可用时间
  uint64_t upgrade_wait_time_;  // 过期持久化数据升级内存数据的等待时间
  int max_write_retry_;         // 持久化重试次数
  uint64_t retry_interval_;     // 持久化重试间隔
};

class CachePersist {
 public:
  explicit CachePersist(Reactor& reactor);

  ~CachePersist() {}

  // 初始化配置
  ReturnCode Init(Config* config);

  // 获取位置信息
  std::unique_ptr<Location> LoadLocation();

  // 持久化位置信息
  void PersistLocation(const Location& location);

  // 获取磁盘文件缓存
  ServiceData* LoadServiceData(const ServiceKey& service_key, ServiceDataType data_type);

  // 持久化服务数据
  // data长度为0时删除持久化文件
  void PersistServiceData(const ServiceKey& service_key, ServiceDataType data_type, const std::string& data);

  // 更新缓存文件时间
  void UpdateSyncTime(const ServiceKey& service_key, ServiceDataType data_type);

 private:
  //  构造服务数据持久化文件名
  std::string BuildFileName(const ServiceKey& service_key, ServiceDataType data_type);

 private:
  Reactor& reactor_;
  CachePersistConfig persist_config_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CACHE_CACHE_PERSIST_H_
