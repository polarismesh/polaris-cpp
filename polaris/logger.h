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

#ifndef POLARIS_CPP_POLARIS_LOGGER_H_
#define POLARIS_CPP_POLARIS_LOGGER_H_

#ifndef __STDC_FORMAT_MACROS
#  define __STDC_FORMAT_MACROS
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <mutex>
#include <string>

#include "polaris/log.h"
#include "utils/utils.h"

namespace polaris {

#define LOG_TRACE __FILE__, __LINE__, kTraceLogLevel
#define LOG_DEBUG __FILE__, __LINE__, kDebugLogLevel
#define LOG_INFO __FILE__, __LINE__, kInfoLogLevel
#define LOG_WARN __FILE__, __LINE__, kWarnLogLevel
#define LOG_ERROR __FILE__, __LINE__, kErrorLogLevel
#define LOG_FATAL __FILE__, __LINE__, kFatalLogLevel

#define POLARIS_LOG(LOG_LEVEL, ...) GetLogger()->Log(LOG_LEVEL, ##__VA_ARGS__)

#define POLARIS_LOG_ENABLE(LEVEL) GetLogger()->isLevelEnabled(LEVEL)

#define POLARIS_STAT_LOG(LOG_LEVEL, ...) GetStatLogger()->Log(LOG_LEVEL, ##__VA_ARGS__)

#define POLARIS_CHECK(x, ret)                       \
  if (POLARIS_UNLIKELY(!(x))) {                     \
    POLARIS_LOG(LOG_ERROR, "check failed: %s", #x); \
    return ret;                                     \
  }

#define POLARIS_CHECK_ARGUMENT(x)                            \
  if (POLARIS_UNLIKELY(!(x))) {                              \
    POLARIS_LOG(LOG_ERROR, "check argument failed: %s", #x); \
    return kReturnInvalidArgument;                           \
  }

#define POLARIS_ASSERT(x)                               \
  if (POLARIS_UNLIKELY(!(x))) {                         \
    POLARIS_LOG(LOG_FATAL, "assertion failed: %s", #x); \
    assert(x);                                          \
  }

const char* LogLevelToStr(LogLevel log_level);

class LoggerImpl : public Logger {
 public:
  LoggerImpl(const std::string& log_path, const std::string& log_file_name, int max_file_size, int max_file_no);

  explicit LoggerImpl(const std::string& log_file_name);

  virtual ~LoggerImpl();

  virtual bool isLevelEnabled(LogLevel log_level);

  virtual void SetLogLevel(LogLevel log_level);

  virtual void SetLogFile(int file_size, int file_no);

  virtual void SetLogDir(const std::string& log_dir);

  virtual void Log(const char* file, int line, LogLevel log_level, const char* format, ...)
      __attribute__((format(printf, 5, 6)));

 private:
  void CloseFile();
  void OpenFile();
  void ShiftFile();
  void ShiftFileWithFileLock();

 private:
  friend class LoggerTest_TestFileShift_Test;
  LogLevel log_level_;

  std::string log_path_;
  std::string log_file_name_;
  int max_file_size_;
  int max_file_no_;
  std::mutex lock_;
  FILE* log_file_;
  int cur_file_size_;
  uint64_t next_shift_check_time_;  // 下次检查文件是否需要滚动的时间
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_LOGGER_H_
