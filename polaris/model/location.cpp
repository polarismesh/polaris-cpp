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

#include "location.h"

#include "sync/mutex.h"
#include "utils/string_utils.h"

struct timespec;

namespace polaris {
namespace model {

std::string VersionedLocation::LocationToString() {
  return "{region: " + location_.region + ", zone: " + location_.zone +
         ", campus: " + location_.campus + "}";
}

std::string VersionedLocation::ToString() {
  return LocationToString() + "_" + StringUtils::TypeToStr<int>(version_);
}

ClientLocation::ClientLocation() : version_(0) {}

ClientLocation::~ClientLocation() {}

void ClientLocation::Init(const Location& location) {
  if (location.region.empty() && location.zone.empty() && location.campus.empty()) {
    return;  // location字段全部为空时表示location无效，直接退出
  }
  version_++;
  location_ = location;
  notify_.NotifyAll();
}

bool ClientLocation::WaitInit(uint64_t timeout) { return notify_.Wait(timeout); }

bool ClientLocation::WaitInit(timespec& ts) { return notify_.Wait(ts); }

void ClientLocation::Update(const Location& location) {
  if ((!location.region.empty() || !location.zone.empty() || !location.campus.empty())) {
    // location有字段不为空，表示location有效，加锁更新
    sync::MutexGuard mutex_guard(notify_.GetMutex());
    if (location_.region != location.region || location_.zone != location.zone ||
        location_.campus != location.campus) {
      version_++;
      location_ = location;
    }
  }
  notify_.NotifyAll();
}

void ClientLocation::GetVersionedLocation(VersionedLocation& versioned_location) {
  sync::MutexGuard mutex_guard(notify_.GetMutex());
  versioned_location.version_  = version_;
  versioned_location.location_ = location_;
}

}  // namespace model
}  // namespace polaris
