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

#include "persist_task.h"

#include <fstream>

#include "logger.h"
#include "utils/file_utils.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

namespace polaris {

PersistTask::PersistTask(const std::string& file, const std::string& data, int retry_times,
                         uint64_t interval)
    : TimingTask(interval), file_(file), data_(data), retry_times_(retry_times) {}

void PersistTask::Run() {
  if (data_.empty() ? DoDelete() : DoPersist()) {
    retry_times_ = 0;  // 成功以后不用在重试
  }
}

uint64_t PersistTask::NextRunTime() {
  return retry_times_ > 0 ? Time::GetCurrentTimeMs() + GetInterval() : 0;
}

bool PersistTask::DoPersist() {
  std::string tmp_file_name = file_ + "." + StringUtils::TypeToStr(pthread_self()) + ".tmp";
  std::ofstream tmp_file(tmp_file_name.c_str(), std::ios::out | std::ios::binary);
  if (tmp_file.good()) {
    tmp_file.write(data_.c_str(), data_.size());
    tmp_file.close();
  } else {
    POLARIS_LOG(LOG_ERROR, "persist data to file[%s] error", tmp_file_name.c_str());
    tmp_file.close();
    return false;
  }

  // 原子替换文件
  if (rename(tmp_file_name.c_str(), file_.c_str()) != 0) {
    POLARIS_LOG(LOG_ERROR, "persist data[%s] to file[%s] failed", data_.c_str(), file_.c_str());
    return false;
  }
  POLARIS_STAT_LOG(LOG_INFO, "persist [%s] to [%s] success", data_.c_str(), file_.c_str());
  return true;
}

bool PersistTask::DoDelete() {
  if (!FileUtils::FileExists(file_)) {  // 文件不存在不用删除
    return true;
  }

  if (remove(file_.c_str()) == 0) {
    POLARIS_STAT_LOG(LOG_INFO, "delete persist data file[%s] success", file_.c_str());
    return true;
  } else {
    POLARIS_LOG(LOG_ERROR, "delete persist data file[%s] failed", file_.c_str());
    return false;
  }
}

void PersistRefreshTimeTask::Run() { FileUtils::UpdateModifiedTime(file_); }

}  // namespace polaris
