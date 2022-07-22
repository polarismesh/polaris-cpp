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

#include "utils/time_clock.h"

#include <features.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <mutex>

#include "utils/fork.h"
#include "utils/utils.h"

namespace polaris {

uint64_t Time::kMaxTime = 0xffffffffffffffff;
uint64_t Time::kThousandBase = 1000ull;
uint64_t Time::kMillionBase = 1000000ull;
uint64_t Time::kBillionBase = 1000000000ull;

// 获取当前时间
static uint64_t clock_real_time() {
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return ts.tv_sec * Time::kThousandBase + ts.tv_nsec / Time::kMillionBase;
}

static uint64_t clock_monotonic_time() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * Time::kThousandBase + ts.tv_nsec / Time::kMillionBase;
}

static uint64_t clock_monotonic_time_coarse() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
  return ts.tv_sec * Time::kThousandBase + ts.tv_nsec / Time::kMillionBase;
}

// 获取时间函数指针
static TimeFunction system_time_func = clock_real_time;
static TimeFunction steady_time_func = clock_monotonic_time;
static TimeFunction steady_time_coarse_func = clock_monotonic_time_coarse;

uint64_t Time::GetSystemTimeMs() { return system_time_func(); }

uint64_t Time::GetSteadyTimeUs() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * Time::kMillionBase + ts.tv_nsec / Time::kThousandBase;
}

uint64_t Time::SteadyTimeDiff(const timespec& ts) {
  uint64_t other_time = ts.tv_sec * Time::kThousandBase + ts.tv_nsec / Time::kMillionBase;
  uint64_t current_time = steady_time_func();
  return other_time > current_time ? other_time - current_time : 0;
}

timespec Time::SteadyTimeAdd(uint64_t add_ms) {
  uint64_t current_time = steady_time_func() + add_ms;
  timespec ts;
  ts.tv_sec = current_time / Time::kThousandBase;
  ts.tv_nsec = (current_time % Time::kThousandBase) * Time::kMillionBase;
  return ts;
}

uint64_t Time::GetCoarseSteadyTimeMs() { return steady_time_coarse_func(); }

uint64_t Time::CoarseSteadyTimeSub(uint64_t sub_ms) {
  uint64_t current_time = steady_time_coarse_func();
  return current_time > sub_ms ? current_time - sub_ms : 0;
}

void Time::SetCustomTimeFunc(TimeFunction custom_system_func, TimeFunction custom_steady_func) {
  system_time_func = custom_system_func;
  steady_time_func = custom_steady_func;
  steady_time_coarse_func = custom_steady_func;
}

void Time::SetDefaultTimeFunc() {
  system_time_func = clock_real_time;
  steady_time_func = clock_monotonic_time;
  steady_time_coarse_func = clock_monotonic_time_coarse;
}

// 自定义时钟实现，使用线程定时更新该数值
std::atomic<uint64_t> g_custom_system_time(0);
std::atomic<uint64_t> g_custom_steady_time(0);

int g_custom_clock_ref_count;  // 记录有多少Context使用，最后一个Context关闭线程
std::mutex g_custom_clock_lock;
pthread_t g_custom_clock_update_tid = 0;

void* clock_thread_update_time(void* /*arg*/) {
  while (g_custom_clock_ref_count > 0) {
    uint64_t real_time = clock_real_time();
    if (real_time > g_custom_system_time.load(std::memory_order_relaxed)) {
      g_custom_system_time.store(real_time, std::memory_order_relaxed);
    }
    real_time = clock_monotonic_time();
    if (real_time > g_custom_steady_time.load(std::memory_order_relaxed)) {
      g_custom_steady_time.store(real_time, std::memory_order_relaxed);
    }
    usleep(1000);
  }
  Time::SetDefaultTimeFunc();  // 还原成真实的时钟函数
  return nullptr;
}

#if defined(POLARIS_ENABLE_TIME_TICKER)  // 不使用自定义时钟
static uint64_t custom_clock_system_time() { return g_custom_system_time.load(std::memory_order_relaxed); }
static uint64_t custom_clock_steady_time() { return g_custom_steady_time.load(std::memory_order_relaxed); }
#endif

void Time::TrySetUpClock() {
  std::lock_guard<std::mutex> lock_guard(g_custom_clock_lock);
  SetupCallbackAtfork();
#if defined(POLARIS_ENABLE_TIME_TICKER)  // 自定义时钟开关
  g_custom_clock_ref_count++;
  if (g_custom_clock_update_tid == 0 && steady_time_func == clock_monotonic_time) {  // 创建更新线程
    // 这里必须先初始化自定义时钟，线程启动后会立即替换
    uint64_t real_time = clock_real_time();
    if (real_time > g_custom_system_time.load(std::memory_order_relaxed)) {
      g_custom_system_time.store(real_time, std::memory_order_relaxed);
    }
    real_time = clock_monotonic_time();
    if (real_time > g_custom_steady_time.load(std::memory_order_relaxed)) {
      g_custom_steady_time.store(real_time, std::memory_order_relaxed);
    }
    // 设置为自定义函数
    system_time_func = custom_clock_system_time;
    steady_time_func = custom_clock_steady_time;
    steady_time_coarse_func = custom_clock_steady_time;
    if (pthread_create(&g_custom_clock_update_tid, nullptr, clock_thread_update_time, nullptr) != 0) {
      Time::SetDefaultTimeFunc();  // 还原成真实的时钟函数
      g_custom_clock_update_tid = 0;
    } else {
#  if defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 12) && !defined(COMPILE_FOR_PRE_CPP11)
      pthread_setname_np(g_custom_clock_update_tid, "time_ticker");
#  endif
    }
  }
#endif
}

void Time::TryShutdomClock() {
  std::lock_guard<std::mutex> lock_guard(g_custom_clock_lock);
#if defined(POLARIS_ENABLE_TIME_TICKER)  // 自定义时钟开关
  g_custom_clock_ref_count--;
  if (g_custom_clock_ref_count <= 0 && g_custom_clock_update_tid != 0) {
    pthread_join(g_custom_clock_update_tid, nullptr);
    g_custom_clock_update_tid = 0;
  }
#endif
}

uint64_t Time::TimestampToUint64(const google::protobuf::Timestamp& timestamp) {
  return timestamp.seconds() * kThousandBase + timestamp.nanos() / kMillionBase;
}

uint64_t Time::DurationToUint64(const google::protobuf::Duration& duration) {
  return duration.seconds() * kThousandBase + duration.nanos() / kMillionBase;
}

void Time::Uint64ToTimestamp(uint64_t time_value, google::protobuf::Timestamp* timestamp) {
  timestamp->set_seconds(time_value / kThousandBase);
  timestamp->set_nanos((time_value % kThousandBase) * kMillionBase);
}

void Time::Uint64ToDuration(uint64_t time_value, google::protobuf::Duration* duration) {
  duration->set_seconds(time_value / kThousandBase);
  duration->set_nanos((time_value % kThousandBase) * kMillionBase);
}

}  // namespace polaris
