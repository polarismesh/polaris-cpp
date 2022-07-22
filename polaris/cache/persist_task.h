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

#ifndef POLARIS_CPP_POLARIS_CACHE_PERSIST_TASK_H_
#define POLARIS_CPP_POLARIS_CACHE_PERSIST_TASK_H_

#include <stdint.h>

#include <string>

#include "reactor/task.h"

namespace polaris {

// 服务数据持久化异步任务
class PersistTask : public TimingTask {
 public:
  PersistTask(const std::string& file, const std::string& data, int retry_times, uint64_t interval);

  virtual void Run();

  virtual uint64_t NextRunTime();

  bool DoPersist();  // 执行持久化写入操作

  bool DoDelete();  // 执行删除持久化文件操作

 private:
  std::string file_;  // 持久化文件名
  std::string data_;  // 持久化数据
  int retry_times_;   // 剩余重试次数
};

// 执行刷新磁盘文件缓存时间任务
class PersistRefreshTimeTask : public Task {
 public:
  explicit PersistRefreshTimeTask(const std::string& file) : file_(file) {}

  virtual void Run();

 private:
  std::string file_;  // 文件名
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_CACHE_PERSIST_TASK_H_
