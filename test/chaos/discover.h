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

#ifndef POLARIS_CPP_TEST_CHAOS_DISCOVER_H_
#define POLARIS_CPP_TEST_CHAOS_DISCOVER_H_

#include "base.h"

#include "polaris/polaris.h"

namespace polaris {

class DiscoverChaos : public ChaosBase {
 public:
  DiscoverChaos();

  bool Init(Config* config);

  virtual bool SetUp();

  virtual void TearDown();

  virtual void Run();

 private:
  int SelectNextPort();

  bool PrepareData();

 private:
  ServiceKey service_key_;
  std::string token_;
  std::set<int> port_set;
  int last_deregister_port_;
  std::size_t instance_num_;

  ProviderApi* provider_;
  ConsumerApi* consumer_;         // 常规使用
  ConsumerApi* timing_consumer_;  // 每24小时获取一次服务
  ConsumerApi* idle_consumer_;    // 发现一次服务后，一直保持空闲不使用状态
};

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_CHAOS_DISCOVER_H_