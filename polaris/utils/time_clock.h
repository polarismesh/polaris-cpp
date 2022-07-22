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

#ifndef POLARIS_CPP_POLARIS_UTILS_TIME_CLOCK_H_
#define POLARIS_CPP_POLARIS_UTILS_TIME_CLOCK_H_

#include <stdint.h>
#include <time.h>

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

namespace polaris {

/// @brief 时间获取函数定义
///
/// 该函数会返回当前的时间，单位为毫秒ms
using TimeFunction = uint64_t (*)();

/// @brief 毫秒级时间类，用于获取当前时间，及常用时间函数
///
/// 主要有两种类型的时间函数：
///  - System：系统时钟，该时钟是系统当前显示时间，对应时间戳方法获取从1970-1-1起始的时间戳，该时间戳可能会跳变
///  - Steady：稳定时间，该时钟是系统稳定递增时间，对应时间戳方法获取从系统启动开始的时间戳，该时间戳不会跳变
class Time {
 public:
  /// @brief 获取当前系统时间，时间可能跳变，用于不需要进行比较情况，例如日志输出
  static void GetSystemClockTime(timespec& ts) { clock_gettime(CLOCK_REALTIME, &ts); }

  /// @brief 获取当前系统时钟毫秒级时间戳
  static uint64_t GetSystemTimeMs();

  /// @brief 获取当前单调时钟微秒级时间戳
  static uint64_t GetSteadyTimeUs();

  /// @brief 获取某个时间减去当前单调时间戳的差值，用于计算剩余请求超时时间
  ///
  /// @param ts 要计算的时间
  /// @return uint64_t 减去当前时间剩余毫秒数
  static uint64_t SteadyTimeDiff(const timespec& ts);

  /// @brief 获取当前单调时间戳并加上对应毫秒后返回，用于计算请求超时截止时间
  ///
  /// @param add_ms 希望加上的毫秒数
  /// @return timespec 当前单调时间加上对应毫秒之后的时间
  static timespec SteadyTimeAdd(uint64_t add_ms);

  /// @brief 获取当前单调递增时钟毫秒级时间戳，精度稍低
  static uint64_t GetCoarseSteadyTimeMs();

  /// @brief 获取当前单调时间戳并减去对应毫秒后返回，用于计算清理时间
  ///
  /// @param sub_ms 希望减去的毫秒数
  /// @return uint64_t 当前单调时间减去毫秒数后的时间戳，如果当前单调时间戳小于期望减去的时间，则返回0
  static uint64_t CoarseSteadyTimeSub(uint64_t sub_ms);

  /// @brief 尝试启动自定义时钟
  static void TrySetUpClock();

  /// @brief 尝试关闭自定义时钟
  static void TryShutdomClock();

  /// @brief 设置北极星SDK获取当前时间的函数
  ///        业务在创建北极星对象前设置自定义时间函数，即使开了时间线程开关也不会启动时间ticker线程
  ///
  /// @param custom_system_func 用户自定义时间获取函数，该函数访问当前系统时钟对应的时间戳，单位为毫秒ms
  /// @param custom_steady_func 用户自定义时间获取函数，该函数返回当前稳定时钟的时间戳，单位为毫秒ms
  static void SetCustomTimeFunc(TimeFunction custom_system_func, TimeFunction custom_steady_func);

  /// @brief 将北极星SDK时间函数设置默认的时间函数
  ///        设置成默认的时间函数以后，再创建北极星SDK对象，如果开启了时间线程开关，则会尝试启动时间ticker线程
  static void SetDefaultTimeFunc();

  static uint64_t TimestampToUint64(const google::protobuf::Timestamp& timestamp);

  static uint64_t DurationToUint64(const google::protobuf::Duration& duration);

  static void Uint64ToTimestamp(uint64_t time_value, google::protobuf::Timestamp* timestamp);

  static void Uint64ToDuration(uint64_t time_value, google::protobuf::Duration* duration);

  static uint64_t kMaxTime;
  static uint64_t kThousandBase;
  static uint64_t kMillionBase;
  static uint64_t kBillionBase;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_UTILS_TIME_CLOCK_H_
