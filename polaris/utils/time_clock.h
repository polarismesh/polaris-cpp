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

namespace google {
namespace protobuf {
class Duration;
class Timestamp;
}  // namespace protobuf
}  // namespace google

namespace polaris {

/// @brief 毫秒级时间类，用于获取当前时间，及常用时间函数
///
/// 1. 默认实现使用系统调用获取当前时间
/// 2. 当前进程首次创建Context时，会尝试启动自定义时钟，该时钟为启动后台线程每毫秒更新一下时钟。
///    当最后一个Context关闭时，会尝试关闭自定义时钟，并将函数还原成使用系统函数获取时间
/// 3. 此外也可以设置成伪时钟，供测时使用
class Time {
public:
  /// @brief 获取当前毫秒级时间
  ///
  /// @return uint64_t
  static uint64_t GetCurrentTimeMs();

  /// @brief 获取当前微秒级时间
  ///
  /// @return uint64_t
  static uint64_t GetCurrentTimeUs();

  /// @brief 获取某个时间减去当前时间的差值
  ///
  /// @param ts 要计算的时间
  /// @return uint64_t 减去当前时间剩余毫秒数
  static uint64_t DiffMsWithCurrentTime(const timespec& ts);

  /// @brief 获取当前时间加上差值后返回
  ///
  /// @param add_ms 希望加上的差值
  /// @return const timespec& 当前时间加上差值之后的时间
  static timespec CurrentTimeAddWith(uint64_t add_ms);

  /// @brief 尝试启动自定义时钟
  static void TrySetUpClock();

  /// @brief 尝试关闭自定义时钟
  static void TryShutdomClock();

  /// @brief fork时回调函数
  static void ForkPrepare();

  /// @brief Fork完成子进程回调函数
  static void ForkChild();

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
