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

#include "instance_not_exist.h"

namespace polaris {

bool InstanceNotExist::SetUp() {
  consumer_ = ConsumerApi::CreateWithDefaultFile();
  if (consumer_ == nullptr) {
    CHAOS_INFO("crate consumer api failed");
    return false;
  }
  return true;
}

void InstanceNotExist::Run() {
  int next_run_time = 0;
  CHAOS_INFO("begin run loop");
  while (!stop_received_) {
    if (next_run_time <= time(nullptr)) {
      ServiceKey service_key = {"Test", "polaris.cpp.chaos.instance_not_exist"};
      GetOneInstanceRequest request(service_key);

      Instance instance;
      ReturnCode ret_code = consumer_->GetOneInstance(request, instance);
      if (ret_code != kReturnInstanceNotFound) {
        CHAOS_ERROR("discover not exist service return %s", ReturnCodeToMsg(ret_code).c_str());
      }
      next_run_time = time(nullptr) + 60 * 5;
    }
    sleep(1);
  }
  CHAOS_INFO("exit loop");
}

void InstanceNotExist::TearDown() {
  if (consumer_ != nullptr) {
    delete consumer_;
    consumer_ = nullptr;
  }
}

}  // namespace polaris
