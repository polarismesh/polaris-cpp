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

#include "logger.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

#include <set>

#include "polaris/log.h"
#include "utils/file_utils.h"
#include "utils/indestructible.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

namespace polaris {

#define gettid() syscall(SYS_gettid)

const char* LogLevelToStr(LogLevel log_level) {
  switch (log_level) {
    case kTraceLogLevel:
      return "TRACE";
    case kDebugLogLevel:
      return "DEBUG";
    case kInfoLogLevel:
      return "INFO";
    case kWarnLogLevel:
      return "WARN";
    case kErrorLogLevel:
      return "ERROR";
    case kFatalLogLevel:
      return "FATAL";
    default:
      return "NULL";
  }
}

LoggerImpl::LoggerImpl(const std::string& log_path, const std::string& log_file_name,
                       int max_file_size, int max_file_no)
    : log_level_(kInfoLogLevel), log_path_(log_path), log_file_name_(log_file_name),
      max_file_size_(max_file_size), max_file_no_(max_file_no), log_file_(NULL), cur_file_size_(0),
      shift_check_time_(0) {
  if (max_file_no_ < 1) {
    max_file_no_ = 1;
  }
}

static const char kLogDefaultPath[]     = "$HOME/polaris/log/";
static const char kLogDefaultFile[]     = "polaris.log";
static const char kLogDefaultStatFile[] = "stat.log";
static const int kLogMaxFileSize        = 32 * 1024 * 1024;  // 32M
static const int kLogMaxFileNo          = 20;

LoggerImpl::LoggerImpl(const std::string& log_file_name)
    : log_level_(kInfoLogLevel), log_path_(kLogDefaultPath), log_file_name_(log_file_name),
      max_file_size_(kLogMaxFileSize), max_file_no_(kLogMaxFileNo), log_file_(NULL),
      cur_file_size_(0), shift_check_time_(0) {}

LoggerImpl::~LoggerImpl() { CloseFile(); }

bool LoggerImpl::isLevelEnabled(LogLevel log_level) { return log_level >= log_level_; }

void LoggerImpl::SetLogLevel(LogLevel log_level) { log_level_ = log_level; }

void LoggerImpl::SetLogDir(const std::string& log_dir) {
  sync::MutexGuard mutex_guard(lock_);
  CloseFile();
  log_path_ = log_dir;
  ShiftFile();
}

void LoggerImpl::CloseFile() {
  if (log_file_ != NULL) {
    fflush(log_file_);
    fclose(log_file_);
    log_file_ = NULL;
  }
}

void LoggerImpl::OpenFile() {
  std::string file_name = log_path_ + "/" + log_file_name_;
  FILE* new_log_file;
  if ((new_log_file = fopen(file_name.c_str(), "a")) != NULL) {
    fseek(new_log_file, 0, SEEK_END);
    cur_file_size_    = ftell(new_log_file);
    shift_check_time_ = Time::GetCurrentTimeMs();
    log_file_         = new_log_file;
  } else {
    fprintf(stderr, "create log file with errno:%d\n", errno);
  }
}

void LoggerImpl::ShiftFileWithFileLock() {
  // 检查当前文件大小
  bool need_shift = false;
  FILE* cur_log_file;
  std::string cur_file_name = log_path_ + "/" + log_file_name_;
  if ((cur_log_file = fopen(cur_file_name.c_str(), "a")) != NULL) {
    fseek(cur_log_file, 0, SEEK_END);
    cur_file_size_ = ftell(cur_log_file);
    fclose(cur_log_file);
    need_shift = cur_file_size_ >= max_file_size_;
  }

  if (need_shift) {
    // 列出当前目录下的文件，计算当前日志文件数量
    std::set<std::string> log_file_set;
    std::string file_name;
    DIR* dp = opendir(log_path_.c_str());
    if (dp != NULL) {
      struct dirent* dirp;
      while ((dirp = readdir(dp)) != NULL) {
        file_name = std::string(dirp->d_name);
        if (file_name.find(log_file_name_ + ".") != std::string::npos) {
          log_file_set.insert(file_name);
        }
      }
    }
    closedir(dp);
    // 滚动文件
    file_name = log_file_name_ + "." + StringUtils::TypeToStr<int>(max_file_no_ - 1);
    std::string shift_file_name = log_path_ + "/" + file_name;
    if (log_file_set.find(file_name) != log_file_set.end()) {
      unlink(shift_file_name.c_str());
    }
    for (int i = max_file_no_ - 2; i >= 0; --i) {
      file_name = log_file_name_ + "." + StringUtils::TypeToStr<int>(i);
      if (log_file_set.find(file_name) != log_file_set.end()) {
        file_name = log_path_ + "/" + file_name;
        rename(file_name.c_str(), shift_file_name.c_str());
        shift_file_name = file_name;
      } else {
        shift_file_name = log_path_ + "/" + file_name;
      }
    }
    // 重命名成功才修改当前编号
    if (rename(cur_file_name.c_str(), shift_file_name.c_str()) != 0) {
      fprintf(stderr, "shift log file %s with errno:%d\n", shift_file_name.c_str(), errno);
    }
  }
}

static const uint64_t kShiftCheckInterval = 10 * 1000;
void LoggerImpl::ShiftFile() {
  if (log_file_ != NULL) {
    uint64_t time_now = Time::GetCurrentTimeMs();
    if (time_now > shift_check_time_ + kShiftCheckInterval) {
      shift_check_time_ = time_now;
      fseek(log_file_, 0, SEEK_END);
      cur_file_size_ = ftell(log_file_);
    }
  } else {  // 在该目录下首次输出日志，创建目录，打开文件
    log_path_ = FileUtils::ExpandPath(log_path_);
    if (!FileUtils::CreatePath(log_path_)) {
      fprintf(stderr, "polaris c++ sdk create log path[%s] failed\n", log_path_.c_str());
    }
    OpenFile();
  }
  if (cur_file_size_ >= max_file_size_) {  // 需要切换文件
    CloseFile();
    // 先尝试加文件锁
    std::string lock_file = log_path_ + "/log.lock";
    int fd                = open(lock_file.c_str(), O_RDWR | O_CREAT,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd >= 0) {
      if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        ShiftFileWithFileLock();
        flock(fd, LOCK_UN);
      }
      close(fd);
    }
    OpenFile();
  }
}

void LoggerImpl::Log(const char* file, int line, LogLevel log_level, const char* format, ...) {
  if (log_level < log_level_) {
    return;
  }
  char* message = NULL;
  va_list args;
  va_start(args, format);
  if (vasprintf(&message, format, args) == -1) {
    va_end(args);
    return;
  }
  va_end(args);

  static __thread uint32_t tid = 0;
  if (tid == 0) {
    tid = gettid();
  }

  const char* display_file;
  const char* final_slash = strrchr(file, '/');
  if (final_slash == NULL) {
    display_file = file;
  } else {
    display_file = final_slash + 1;
  }

  timespec now = Time::CurrentTimeAddWith(0);
  struct tm tm;
  char time_buffer[64];
  time_t timer = static_cast<time_t>(now.tv_sec);
  if (!localtime_r(&timer, &tm)) {
    snprintf(time_buffer, sizeof(time_buffer), "error:localtime");
  } else if (0 == strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &tm)) {
    snprintf(time_buffer, sizeof(time_buffer), "error:strftime");
  }

  do {
    sync::MutexGuard mutex_guard(lock_);
    ShiftFile();
    if (log_file_ != NULL) {
      int write_size = fprintf(log_file_, "[%s,%03ld] %s %s (tid:%" PRId32 " %s:%d)\n", time_buffer,
                               now.tv_nsec / 1000000L, LogLevelToStr(log_level), message, tid,
                               display_file, line);
      cur_file_size_ += write_size;
      fflush(log_file_);
    }
  } while (false);
  free(message);
}

Logger* g_logger      = NULL;
Logger* g_stat_logger = NULL;

void SetLogger(Logger* logger) {
  if (logger == NULL) {  // 不允许设置为NULL，从而保证使用时不用判断
    POLARIS_LOG(LOG_WARN, "Set logger to NULL change the logger to sdk default logger");
    g_logger = NULL;
    POLARIS_LOG(LOG_WARN, "Set logger to NULL change the logger to sdk default logger");
    return;
  } else {
    g_logger = logger;
  }
}

void SetStatLogger(Logger* logger) {
  if (logger == NULL) {  // 不允许设置为NULL，从而保证使用时不用判断
    POLARIS_STAT_LOG(LOG_WARN, "Set stat logger to NULL change the logger to default stat logger");
    g_stat_logger = NULL;
    POLARIS_STAT_LOG(LOG_WARN, "Set logger to NULL change the logger to sdk default logger");
  } else {
    g_stat_logger = logger;
  }
}

void SetLogDir(const std::string& log_dir) {
  GetLogger()->SetLogDir(log_dir);
  GetStatLogger()->SetLogDir(log_dir);
}

Logger* GetLogger() {
  static Indestructible<LoggerImpl> default_logger(kLogDefaultFile);
  if (g_logger == NULL) {
    return default_logger.Get();
  } else {
    return g_logger;
  }
}

Logger* GetStatLogger() {
  static Indestructible<LoggerImpl> default_stat_logger(kLogDefaultStatFile);
  if (g_stat_logger == NULL) {
    return default_stat_logger.Get();
  } else {
    return g_stat_logger;
  }
}

}  // namespace polaris
