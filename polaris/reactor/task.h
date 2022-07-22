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

#ifndef POLARIS_CPP_POLARIS_REACTOR_TASK_H_
#define POLARIS_CPP_POLARIS_REACTOR_TASK_H_

#include <stdint.h>

#include <map>

namespace polaris {

// 任务接口
class Task {
 public:
  virtual ~Task() {}

  virtual void Run() = 0;  // 任务执行逻辑，只会调用一次
};

// 封装对象方法的任务
template <typename T>
class FuncTask : public Task {
 public:
  typedef void (*Func)(T* para);  // 封装的参数定义

  FuncTask(void (*func)(T* para), T* para) : func_(func), para_(para) {}

  virtual void Run() { func_(para_); }

 protected:
  const Func func_;
  T* const para_;
};

// 封装ServiceBase的类专用的方法任务
template <typename T>
class FuncRefTask : public FuncTask<T> {
 public:
  FuncRefTask(void (*func)(T* para), T* para) : FuncTask<T>(func, para) { FuncTask<T>::para_->IncrementRef(); }

  virtual ~FuncRefTask() { FuncTask<T>::para_->DecrementRef(); }
};

// 定时执行任务接口
class TimingTask : public Task {
 public:
  explicit TimingTask(uint64_t interval) : interval_(interval) {}

  uint64_t GetInterval() const { return interval_; }

  // 下一次执行的时间
  // 返回0的话则释放任务，不再执行
  virtual uint64_t NextRunTime() { return 0; }

 protected:
  const uint64_t interval_;
};

// 将函数封装成定时任务
template <typename T>
class TimingFuncTask : public TimingTask {
 public:
  typedef void (*Func)(T* para);

  TimingFuncTask(void (*func)(T* para), T* para, uint64_t timeout) : TimingTask(timeout), func_(func), para_(para) {}

  virtual void Run() { func_(para_); }

 protected:
  const Func func_;
  T* const para_;
};

template <typename T>
class TimingFuncRefTask : public TimingFuncTask<T> {
 public:
  TimingFuncRefTask(void (*func)(T* para), T* para, uint64_t timeout) : TimingFuncTask<T>(func, para, timeout) {
    TimingFuncTask<T>::para_->IncrementRef();
  }

  virtual ~TimingFuncRefTask() { TimingFuncTask<T>::para_->DecrementRef(); }
};

// 用于延迟删除对象的任务
template <typename T>
class DeferDeleteTask : public Task {
 public:
  explicit DeferDeleteTask(T* object) : object_(object) {}

  virtual ~DeferDeleteTask() { Run(); }

  virtual void Run() {
    if (object_ != nullptr) {
      delete object_;
      object_ = nullptr;
    }
  }

 private:
  T* object_;
};

typedef std::multimap<uint64_t, TimingTask*>::iterator TimingTaskIter;

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_REACTOR_TASK_H_
