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

#include "model/location.h"

#include <mutex>

namespace polaris {

std::string Location::ToString() const {
  return "{region: " + region + ", zone: " + zone + ", campus: " + campus + "}";
}

bool Location::IsValid() const { return !region.empty() || !zone.empty() || !campus.empty(); }

void ClientLocation::Init(const Location& location, bool enable_update) {
  enable_update_ = enable_update;
  if (location.IsValid()) {
    version_++;
    location_ = location;
    notify_.NotifyAll();
  }
  if (!enable_update) {
    notify_.NotifyAll();  // 不更新位置信息时，通知其他任务不用等待
  }
}

bool ClientLocation::WaitInit(uint64_t timeout) { return notify_.WaitFor(timeout); }

void ClientLocation::Update(const Location& location) {
  if (enable_update_ && location.IsValid()) {
    const std::lock_guard<std::mutex> mutex_guard(notify_.GetMutex());
    if (location_.region != location.region || location_.zone != location.zone || location_.campus != location.campus) {
      version_++;
      location_ = location;
    }
  }
  notify_.NotifyAll();
}

void ClientLocation::GetLocation(Location& location) {
  const std::lock_guard<std::mutex> mutex_guard(notify_.GetMutex());
  location = location_;
}

void ClientLocation::GetLocation(Location& location, uint32_t& version) {
  const std::lock_guard<std::mutex> mutex_guard(notify_.GetMutex());
  version = version_;
  location = location_;
}

std::string ClientLocation::ToString(const Location& location, uint32_t version) {
  return location.ToString() + "_" + std::to_string(version);
}

}  // namespace polaris
