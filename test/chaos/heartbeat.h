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

#ifndef POLARIS_CPP_TEST_CHAOS_HEARTBEAT_H_
#define POLARIS_CPP_TEST_CHAOS_HEARTBEAT_H_

#include <unistd.h>

#include "base.h"
#include "polaris/consumer.h"
#include "polaris/provider.h"

namespace polaris {

class HeartbeatChaos : public ChaosBase {
 public:
  HeartbeatChaos();

  bool Init(Config* config);

  virtual bool SetUp();

  virtual void Run();

  virtual void TearDown();

 private:
  ProviderApi* provider_;
  ConsumerApi* consumer_;

  polaris::ServiceKey service_key_;
  std::string token_;
  std::string normal_instance_id_;
};

};  // namespace polaris

#endif  // POLARIS_CPP_TEST_CHAOS_HEARTBEAT_H_