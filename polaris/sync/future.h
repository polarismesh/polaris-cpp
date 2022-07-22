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

#ifndef POLARIS_CPP_POLARIS_SYNC_FUTURE_H_
#define POLARIS_CPP_POLARIS_SYNC_FUTURE_H_

#include <errno.h>
#include <time.h>

#include <atomic>
#include <list>

#include "polaris/defs.h"
#include "sync/cond_var.h"
#include "utils/utils.h"

namespace polaris {

template <typename T>
class Promise;
template <typename T>
class Future;

template <typename T>
class SharedState {
 public:
  SharedState() : state_ref_(1) {
    value_ = nullptr;
    ret_code_ = kReturnOk;
  }

  ~SharedState() {
    if (value_ != nullptr) {
      delete value_;
      value_ = nullptr;
    }
  }

  bool IsReady() const { return is_ready_.IsNotified(); }

  bool IsFailed() const { return is_ready_.IsNotified() && ret_code_ != kReturnOk; }

  bool Wait(uint64_t timeout) {
    if (timeout == 0) {
      return is_ready_.IsNotified();
    }
    return is_ready_.WaitFor(timeout);
  }

  T* GetValue() {
    T* return_value = value_;
    value_ = nullptr;
    return return_value;
  }

  ReturnCode GetErrorCode() { return ret_code_; }

  void Reference() { state_ref_.fetch_add(1, std::memory_order_relaxed); }

  void Release() {
    int ref_left = state_ref_.fetch_sub(1, std::memory_order_acq_rel);
    if (ref_left == 1) {
      delete this;
    }
  }

  void SetValue(T* value) {
    value_ = value;
    is_ready_.NotifyAll();
  }

  void SetError(ReturnCode ret_code) {
    ret_code_ = ret_code;
    is_ready_.NotifyAll();
  }

 private:
  std::atomic<int> state_ref_;    // 记录state的索引次数
  T* value_;                      // 结果
  ReturnCode ret_code_;           // 错误码
  sync::CondVarNotify is_ready_;  // 是否就绪
};

template <typename T>
class Future {
 public:
  explicit Future(SharedState<T>* share_state) {
    share_state->Reference();
    share_state_ = share_state;
  }
  ~Future() {
    if (share_state_ != nullptr) {
      share_state_->Release();
    }
  }

  bool IsReady() const { return share_state_->IsReady(); }

  bool IsFailed() const { return share_state_->IsFailed(); }

  bool Wait(uint64_t timeout) { return share_state_->Wait(timeout); }

  T* GetValue() { return share_state_->GetValue(); }

  ReturnCode GetError() { return share_state_->GetErrorCode(); }

 private:
  SharedState<T>* share_state_;
};

template <typename T>
class Promise {
 public:
  Promise() { share_state_ = new SharedState<T>(); }
  ~Promise() { share_state_->Release(); }

  void SetValue(T* value) { share_state_->SetValue(value); }

  void SetError(ReturnCode ret_code) { share_state_->SetError(ret_code); }

  bool IsReady() { return share_state_->IsReady(); }

  bool IsFailed() { return share_state_->IsFailed(); }

  Future<T>* GetFuture() { return new Future<T>(share_state_); }

 private:
  SharedState<T>* share_state_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_SYNC_FUTURE_H_
