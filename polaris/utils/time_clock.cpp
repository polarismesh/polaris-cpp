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

namespace polaris {

// 通过系统调用获取当前时间
static void clock_real_time(timespec& ts) { clock_gettime(CLOCK_REALTIME, &ts); }

// 获取时间函数指针
void (*current_time_impl)(timespec& ts) = clock_real_time;

uint64_t Time::kMaxTime      = 0xffffffffffffffff;
uint64_t Time::kThousandBase = 1000ull;
uint64_t Time::kMillionBase  = 1000000ull;
uint64_t Time::kBillionBase  = 1000000000ull;

uint64_t Time::GetCurrentTimeMs() {
  timespec ts;
  current_time_impl(ts);
  return ts.tv_sec * Time::kThousandBase + ts.tv_nsec / Time::kMillionBase;
}

uint64_t Time::GetCurrentTimeUs() {
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return ts.tv_sec * Time::kMillionBase + ts.tv_nsec / Time::kThousandBase;
}

uint64_t Time::DiffMsWithCurrentTime(const timespec& ts) {
  timespec current_ts;
  current_time_impl(current_ts);
  uint64_t timeout = 0;
  if ((ts.tv_nsec > current_ts.tv_nsec)) {
    timeout += (ts.tv_nsec - current_ts.tv_nsec) / Time::kMillionBase;
  }
  if (ts.tv_sec > current_ts.tv_sec) {
    timeout += (ts.tv_sec - current_ts.tv_sec) * Time::kThousandBase;
  }
  return timeout;
}

timespec Time::CurrentTimeAddWith(uint64_t add_ms) {
  timespec ts;
  current_time_impl(ts);
  ts.tv_nsec += (add_ms % Time::kThousandBase) * Time::kMillionBase;
  ts.tv_sec += add_ms / Time::kThousandBase + ts.tv_nsec / Time::kBillionBase;
  ts.tv_nsec = ts.tv_nsec % Time::kBillionBase;
  return ts;
}

// 自定义时钟实现，使用线程定时更新该数值
volatile uint64_t g_custom_clock_time = 0;
volatile int g_custom_clock_ref_count = 0;  // 记录有多少Context使用，最后一个Context关闭线程
pthread_mutex_t g_custom_clock_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_t g_custom_clock_update_tid = 0;

static void custom_clock_current_time(timespec& ts) {
#ifdef __i386__  // 32位编译时使用原子操作
  uint64_t current_clock_time = ATOMIC_ADD(&g_custom_clock_time, 0);  // 原子读
#else
  uint64_t current_clock_time = g_custom_clock_time;
#endif
  ts.tv_sec  = current_clock_time / Time::kThousandBase;
  ts.tv_nsec = (current_clock_time % Time::kThousandBase) * Time::kMillionBase;
}

void* clock_thread_update_time(void* /*arg*/) {
  while (g_custom_clock_ref_count > 0) {
    timespec clock_time;
    clock_gettime(CLOCK_REALTIME, &clock_time);
    uint64_t real_time =
        clock_time.tv_sec * Time::kThousandBase + clock_time.tv_nsec / Time::kMillionBase;
#ifdef __i386__  // 32位编译时使用原子操作
    uint64_t current_clock_time = ATOMIC_ADD(&g_custom_clock_time, 0);  // 原子读
    if (real_time > current_clock_time) {
      uint64_t add_time = real_time - current_clock_time;
      ATOMIC_ADD(&g_custom_clock_time, add_time);
    }
#else
    if (real_time > g_custom_clock_time) {
      g_custom_clock_time = real_time;
    }
#endif
    usleep(1000);
  }
  current_time_impl = clock_real_time;  // 还原成真实的时钟函数
  return NULL;
}

void Time::TrySetUpClock() {
#if !defined(POLARIS_DISABLE_TIME_TICKER)  // 不使用自定义时钟
  pthread_mutex_lock(&g_custom_clock_lock);
  g_custom_clock_ref_count++;
  if (g_custom_clock_update_tid == 0 && current_time_impl == clock_real_time) {  // 创建更新线程
    pthread_atfork(ForkPrepare, NULL, ForkChild);  // 注册Fork事件回调
    // 这里必须先初始化自定义时钟，线程启动后会立即替换
    timespec clock_time;
    clock_gettime(CLOCK_REALTIME, &clock_time);
    uint64_t real_time =
        clock_time.tv_sec * Time::kThousandBase + clock_time.tv_nsec / Time::kMillionBase;
    if (real_time > g_custom_clock_time) {
      g_custom_clock_time = real_time;
    }
    current_time_impl = custom_clock_current_time;  // 设置为自定义函数
    if (pthread_create(&g_custom_clock_update_tid, NULL, clock_thread_update_time, NULL) != 0) {
      current_time_impl         = clock_real_time;  // 还原成真实的时钟函数
      g_custom_clock_update_tid = 0;
    } else {
#if defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 12)
      pthread_setname_np(g_custom_clock_update_tid, "time_ticker");
#endif
    }
  }
  pthread_mutex_unlock(&g_custom_clock_lock);
#endif
}

void Time::TryShutdomClock() {
#if !defined(POLARIS_DISABLE_TIME_TICKER)  // 不使用自定义时钟
  pthread_mutex_lock(&g_custom_clock_lock);
  g_custom_clock_ref_count--;
  if (g_custom_clock_ref_count <= 0 && g_custom_clock_update_tid != 0) {
    pthread_join(g_custom_clock_update_tid, NULL);
    g_custom_clock_update_tid = 0;
  }
  pthread_mutex_unlock(&g_custom_clock_lock);
#endif
}

void Time::ForkPrepare() {
#if !defined(POLARIS_SUPPORT_FORK)
  fprintf(stderr, "fork not support, see examples/fork_support/README.md\n");
  abort();
#endif
}

void Time::ForkChild() {
  current_time_impl         = clock_real_time;  // 还原成真实的时钟函数
  g_custom_clock_ref_count  = 0;
  g_custom_clock_update_tid = 0;
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
