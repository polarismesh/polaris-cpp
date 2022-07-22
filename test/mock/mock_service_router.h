//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use
//  this file
//  except in compliance with the License. You may obtain a copy of the License
//  at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the
//  specific
//  language governing permissions and limitations under the License.
//

#ifndef POLARIS_CPP_TEST_MOCK_MOCK_SERVICE_ROUTER_H_
#define POLARIS_CPP_TEST_MOCK_MOCK_SERVICE_ROUTER_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "model/model_impl.h"
#include "plugin/service_router/service_router.h"

namespace polaris {

class MockServiceRouter : public ServiceRouter {
 public:
  ~MockServiceRouter() {
    for (auto item : instance_set_cache_) {
      item->DecrementRef();
    }
  }

  MOCK_METHOD2(Init, ReturnCode(Config *config, Context *context));

  MOCK_METHOD2(DoRoute, ReturnCode(RouteInfo &route_info, RouteResult *route_result));

  MOCK_METHOD0(CollectStat, RouterStatData *());

  static Plugin *MockServiceRouterFactory() { return mock_service_router_list_[mock_service_router_index_++]; }

  static void RegisterMockPlugin() { RegisterPlugin("mockRouter", kPluginServiceRouter, MockServiceRouterFactory); }

  static int mock_service_router_index_;
  static std::vector<MockServiceRouter *> mock_service_router_list_;

  void DropFirstInstance(RouteInfo &route_info, RouteResult *) {
    ServiceInstances *service_instances = route_info.GetServiceInstances();
    ASSERT_TRUE(service_instances != nullptr);
    InstancesSet *instances_set = service_instances->GetAvailableInstances();
    std::vector<Instance *> old_set = instances_set->GetInstances();
    std::vector<Instance *> new_set;
    for (std::size_t i = 1; i < old_set.size(); i++) {
      new_set.push_back(old_set[i]);
    }
    InstancesSet *new_instances_set = new InstancesSet(new_set);
    new_instances_set->GetImpl()->count_++;
    service_instances->UpdateAvailableInstances(new_instances_set);
    instance_set_cache_.push_back(new_instances_set);
  }

 private:
  std::vector<InstancesSet *> instance_set_cache_;
};

int MockServiceRouter::mock_service_router_index_ = 0;
std::vector<MockServiceRouter *> MockServiceRouter::mock_service_router_list_;

void MockServiceRouterInit() {
  MockServiceRouter::mock_service_router_index_ = 0;
  MockServiceRouter::mock_service_router_list_.clear();
  MockServiceRouter::mock_service_router_list_.push_back(new MockServiceRouter());
  MockServiceRouter::mock_service_router_list_.push_back(new MockServiceRouter());
}

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_MOCK_MOCK_SERVICE_ROUTER_H_
