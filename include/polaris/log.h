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

/// @file log.h
/// @brief 本文件定义SDK日志接口
///
#ifndef POLARIS_CPP_INCLUDE_POLARIS_LOG_H_
#define POLARIS_CPP_INCLUDE_POLARIS_LOG_H_

#include <string>

namespace polaris {

/// @brief 日志接口中使用的日志级别定义
///
/// @note Trace和Debug级别会同时输出到stdout和文件，Info及以上级别只输出到文件
enum LogLevel {
  kTraceLogLevel = 0,
  kDebugLogLevel,
  kInfoLogLevel,
  kWarnLogLevel,
  kErrorLogLevel,
  kFatalLogLevel
};

/// @brief 日志级别相关宏定义
#define POLARIS_TRACE __FILE__, __LINE__, kTraceLogLevel
#define POLARIS_DEBUG __FILE__, __LINE__, kDebugLogLevel
#define POLARIS_INFO __FILE__, __LINE__, kInfoLogLevel
#define POLARIS_WARN __FILE__, __LINE__, kWarnLogLevel
#define POLARIS_ERROR __FILE__, __LINE__, kErrorLogLevel
#define POLARIS_FATAL __FILE__, __LINE__, kFatalLogLevel

/// @brief 日志接口，用于用户实现自己的日志输出逻辑
///
/// SDK默认有日志实现，但仍然建议用户实现自己的业务接口
class Logger {
public:
  virtual ~Logger() {}

  /// @brief 用于判断指定日志级别是否输出
  ///
  /// SDK中有些日志相对复杂，在调用 @see Log
  /// 方法之前先判断对应日志级别是否开启来决定是否进行内容组装
  /// @param log_level 需要判断是否输出的日志级别
  /// @return true 对应日志级别需要输出
  /// @return false 对应日志级别无需输出
  virtual bool isLevelEnabled(LogLevel log_level) = 0;

  /// @brief 设置日志的输出级别
  ///
  /// 大于等于该日志级别的相关日志会输出
  /// @param log_level 目标日志级别
  virtual void SetLogLevel(LogLevel log_level) = 0;

  /// @brief 设置日志输出目录
  ///
  /// @param log_dir 日志输出目录
  virtual void SetLogDir(const std::string& log_dir) = 0;

  /// @brief 日志输出执行方法
  ///
  /// @param file 日志输出点所在文件
  /// @param line 日志输出点所在文件行号
  /// @param log_level 日志输出级别
  /// @param format 日志输出格式
  /// @param ... 日志输出可变长参数
  virtual void Log(const char* file, int line, LogLevel log_level, const char* format, ...)
      __attribute__((format(printf, 5, 6))) = 0;
};

/// @brief 设置SDK全局日志对象
///
/// @note SDK只使用该对象进行日志输出，需要用户自己管理该对象的析构
/// @param logger 需要设置的日志对象，为NULL时会将日志重置成默认日志对象
void SetLogger(Logger* logger);

/// @brief 设置SDK统计日志对象
///
/// @note SDK只使用该对象进行统计相关日志输出，需要用户自己管理该对象的析构
/// @param logger 需要设置的日志对象，为NULL时会将日志重置成默认日志对象
void SetStatLogger(Logger* logger);

/// @brief 设置所有Logger的日志输出目录
///
/// @param log_dir 日志输出目录
void SetLogDir(const std::string& log_dir);

/// @brief 获取SDK全局日志对象
Logger* GetLogger();

/// @brief 获取SDK统计日志对象
Logger* GetStatLogger();

}  // namespace polaris

#endif  // POLARIS_CPP_INCLUDE_POLARIS_LOG_H_
