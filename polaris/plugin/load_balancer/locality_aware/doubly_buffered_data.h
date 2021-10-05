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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_LOCALITY_AWARE_DOUBLY_BUFFERED_DATA_H_
#define POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_LOCALITY_AWARE_DOUBLY_BUFFERED_DATA_H_

#include <pthread.h>
#include <vector>

#include "logger.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "utils/utils.h"

namespace polaris {

// 配合pthread_key使用，删除wrapper对象
template <typename T>
static void DeleteObject(void *arg) {
  delete static_cast<T *>(arg);
}

template <typename T>
class DoublyBufferedData {
  // Wrapper类，每个线程一个，管理每个线程的锁
  // 线程读数据时上锁，其他线程可读不可改
  class Wrapper;

public:
  // ScopedPtr类，在读DoublyBufferedData中的数据时传递数据
  // 调用DoublyBufferedData的Read()时，加Wrapper的锁
  // 返回一个ScopedPtr对象，被析构时自动释放Wrapper的锁
  class ScopedPtr : Noncopyable {
    friend class DoublyBufferedData;  // Read时，向 _data 和 _w 赋值
  public:
    ScopedPtr() : data_(NULL), w_(NULL) {}
    ~ScopedPtr() {
      if (w_) {
        w_->EndRead();
      }
    }
    const T *Get() const { return data_; }
    const T &operator*() const { return *data_; }
    const T *operator->() const { return data_; }

  private:
    const T *data_;
    Wrapper *w_;
  };

  DoublyBufferedData();
  ~DoublyBufferedData();

  // 读前台,线程安全的
  int Read(ScopedPtr *ptr);

  // 修改前后台数据，Modify负责线程安全，具体如何改数据，使用Fn传入
  // 在DoublyBufferedData之外只能读到前台数据，传Fn进来之后再将后台绑定上去
  // Modify里传递的是对象，但对应的对象重载了()，可以当成函数调用。
  template <typename Fn>
  size_t Modify(Fn &fn);
  template <typename Fn, typename Arg1>
  size_t Modify(Fn &fn, const Arg1 &);
  template <typename Fn, typename Arg1, typename Arg2>
  size_t Modify(Fn &fn, const Arg1 &, const Arg2 &);

  // Modify会将后台数据绑定到Fn，如果改后台数据时想看前台的情况
  // ModifyWithForeground会调用Modify，拿到后台后，会将前台绑定到函数上
  template <typename Fn>
  size_t ModifyWithForeground(Fn &fn);
  template <typename Fn, typename Arg1>
  size_t ModifyWithForeground(Fn &fn, const Arg1 &);
  template <typename Fn, typename Arg1, typename Arg2>
  size_t ModifyWithForeground(Fn &fn, const Arg1 &, const Arg2 &);

private:
  template <typename Fn>
  struct WithFG0 {
    WithFG0(Fn &fn, T *data) : fn_(fn), data_(data) {}
    size_t operator()(T &bg) {
      // index为0和1，_data是&(_data[0])
      return fn_(bg, (const T &)data_[&bg == data_]);
    }

  private:
    Fn &fn_;
    T *data_;
  };

  template <typename Fn, typename Arg1>
  struct WithFG1 {
    WithFG1(Fn &fn, T *data, const Arg1 &arg1) : fn_(fn), data_(data), arg1_(arg1) {}
    size_t operator()(T &bg) { return fn_(bg, (const T &)data_[&bg == data_], arg1_); }

  private:
    Fn &fn_;
    T *data_;
    const Arg1 &arg1_;
  };

  template <typename Fn, typename Arg1, typename Arg2>
  struct WithFG2 {
    WithFG2(Fn &fn, T *data, const Arg1 &arg1, const Arg2 &arg2)
        : fn_(fn), data_(data), arg1_(arg1), arg2_(arg2) {}
    size_t operator()(T &bg) { return fn_(bg, (const T &)data_[&bg == data_], arg1_, arg2_); }

  private:
    Fn &fn_;
    T *data_;
    const Arg1 &arg1_;
    const Arg2 &arg2_;
  };

  template <typename Fn, typename Arg1>
  struct Closure1 {
    Closure1(Fn &fn, const Arg1 &arg1) : fn_(fn), arg1_(arg1) {}
    size_t operator()(T &bg) { return fn_(bg, arg1_); }

  private:
    Fn &fn_;
    const Arg1 &arg1_;
  };

  template <typename Fn, typename Arg1, typename Arg2>
  struct Closure2 {
    Closure2(Fn &fn, const Arg1 &arg1, const Arg2 &arg2) : fn_(fn), arg1_(arg1), arg2_(arg2) {}
    size_t operator()(T &bg) { return fn_(bg, arg1_, arg2_); }

  private:
    Fn &fn_;
    const Arg1 &arg1_;
    const Arg2 &arg2_;
  };

  const T *UnsafeRead() const { return data_ + index_; }

  Wrapper *AddWrapper();
  void RemoveWrapper(Wrapper *);

private:
  // 前台和后台数据
  T data_[2];
  // 前台的index
  sync::Atomic<int> index_;

  // 创建线程私有的数据
  bool created_key_;
  pthread_key_t wrapper_key_;

  std::vector<Wrapper *> wrappers_;

  // 针对_wrappers上的锁，防止在遍历_wrappers时或增删wrapper出错
  sync::Mutex wrappers_mutex_;

  // 改前后台数据时上的锁
  sync::Mutex modify_mutex_;
};  // class DoublyBufferedData

// static const pthread_key_t INVALID_PTHREAD_KEY = (pthread_key_t)-1;

// Wrapper类，用pthread_key_t确保每个线程一个
template <typename T>
class DoublyBufferedData<T>::Wrapper : private sync::Mutex {
  friend class DoublyBufferedData;

public:
  explicit Wrapper(DoublyBufferedData *c) : control_(c) {}

  ~Wrapper() {
    if (control_ != NULL) {
      control_->RemoveWrapper(this);
    }
  }

  inline void BeginRead() { Lock(); }

  inline void EndRead() { Unlock(); }

  inline void WaitReadDone() {
    Lock();
    Unlock();
  }

private:
  DoublyBufferedData *control_;
};

template <typename T>
typename DoublyBufferedData<T>::Wrapper *DoublyBufferedData<T>::AddWrapper() {
  // 在内存不足时，new (std::nothrow)并不抛出异常,将指针置NULL
  Wrapper *w(new (std::nothrow) Wrapper(this));
  if (NULL == w) {
    return NULL;
  }
  sync::MutexGuard lock(wrappers_mutex_);
  wrappers_.push_back(w);  // 将Wrapper插入_wrappers中
  return w;
}

// Called when thread quits.
// Wrapper析构是会调用该函数
template <typename T>
void DoublyBufferedData<T>::RemoveWrapper(typename DoublyBufferedData<T>::Wrapper *w) {
  if (w == NULL) {
    return;
  }
  sync::MutexGuard lock(wrappers_mutex_);
  for (size_t i = 0; i < wrappers_.size(); ++i) {
    if (wrappers_[i] == w) {
      wrappers_[i] = wrappers_.back();
      wrappers_.pop_back();
      return;
    }
  }
}

template <typename T>
DoublyBufferedData<T>::DoublyBufferedData() : index_(0), created_key_(false), wrapper_key_(0) {
  wrappers_.reserve(64);
  const int ret = pthread_key_create(&wrapper_key_, DeleteObject<Wrapper>);
  if (ret != 0) {
    POLARIS_LOG(LOG_ERROR, "Construct DoublyBufferedData, fail to pthread_key_create: %d", ret);
  } else {
    created_key_ = true;
  }
  data_[0] = T();
  data_[1] = T();
}

template <typename T>
DoublyBufferedData<T>::~DoublyBufferedData() {
  if (created_key_) {
    pthread_key_delete(wrapper_key_);
  }
  {
    sync::MutexGuard lock(wrappers_mutex_);
    for (size_t i = 0; i < wrappers_.size(); ++i) {
      // wrapper在析构时会利用_control主动将自己从_wrappers中删除
      // 将_control设为NULL
      wrappers_[i]->control_ = NULL;
      delete wrappers_[i];
    }
    wrappers_.clear();
  }
}

template <typename T>
int DoublyBufferedData<T>::Read(typename DoublyBufferedData<T>::ScopedPtr *ptr) {
  if (POLARIS_LIKELY(!created_key_)) {
    // pthread_key_t 没有创建成功
    return -1;
  }
  // 当某个线程获取到他的Wrapper时，读数据
  Wrapper *w = static_cast<Wrapper *>(pthread_getspecific(wrapper_key_));
  if (POLARIS_LIKELY(w != NULL)) {
    w->BeginRead();             // 上锁
    ptr->data_ = UnsafeRead();  // 传前台
    ptr->w_    = w;             // 传Wrapper，ScopedPtr析构时会释放锁
    return 0;
  }
  // pthread_getspecific获取Wrapper失败，创建一个线程对应的Wrapper
  w = AddWrapper();
  if (POLARIS_LIKELY(w != NULL)) {
    const int rc = pthread_setspecific(wrapper_key_, w);
    if (rc == 0) {
      w->BeginRead();
      ptr->data_ = UnsafeRead();
      ptr->w_    = w;
      return 0;
    }
  }
  return -1;
}

template <typename T>
template <typename Fn>
size_t DoublyBufferedData<T>::Modify(Fn &fn) {
  // 上锁改后台数据
  sync::MutexGuard lock(modify_mutex_);
  int bg_index     = !index_;
  const size_t ret = fn(data_[bg_index]);
  if (!ret) {
    return 0;
  }
  // 切换前后台，因为切的是index，只要读数据时只取一次index，就不会被影响
  index_ = bg_index;

  bg_index = !bg_index;
  {
    // 针对每个线程（wrapper），取他的锁再释放，确保每个线程结束了读旧前台
    sync::MutexGuard lock(wrappers_mutex_);
    for (size_t i = 0; i < wrappers_.size(); ++i) {
      wrappers_[i]->WaitReadDone();
    }
  }
  // 改旧前台，现在的后台
  const size_t ret2 = fn(data_[bg_index]);
  if (ret2 != ret) {
    POLARIS_LOG(LOG_ERROR,
                "Modify DoublyBufferedData, the return values of fg and bg are different");
  }
  return ret2;
}

template <typename T>
template <typename Fn, typename Arg1>
size_t DoublyBufferedData<T>::Modify(Fn &fn, const Arg1 &arg1) {
  Closure1<Fn, Arg1> c(fn, arg1);
  return Modify(c);
}

template <typename T>
template <typename Fn, typename Arg1, typename Arg2>
size_t DoublyBufferedData<T>::Modify(Fn &fn, const Arg1 &arg1, const Arg2 &arg2) {
  Closure2<Fn, Arg1, Arg2> c(fn, arg1, arg2);
  return Modify(c);
}

template <typename T>
template <typename Fn>
size_t DoublyBufferedData<T>::ModifyWithForeground(Fn &fn) {
  WithFG0<Fn> c(fn, data_);
  return Modify(c);
}

template <typename T>
template <typename Fn, typename Arg1>
size_t DoublyBufferedData<T>::ModifyWithForeground(Fn &fn, const Arg1 &arg1) {
  WithFG1<Fn, Arg1> c(fn, data_, arg1);
  return Modify(c);
}

template <typename T>
template <typename Fn, typename Arg1, typename Arg2>
size_t DoublyBufferedData<T>::ModifyWithForeground(Fn &fn, const Arg1 &arg1, const Arg2 &arg2) {
  WithFG2<Fn, Arg1, Arg2> c(fn, data_, arg1, arg2);
  return Modify(c);
}

}  // namespace polaris
#endif  // POLARIS_CPP_POLARIS_PLUGIN_LOAD_BALANCER_LOCALITY_AWARE_DOUBLY_BUFFERED_DATA_H_