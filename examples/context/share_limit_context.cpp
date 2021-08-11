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

#include <assert.h>

#include <iostream>

#include "polaris/consumer.h"
#include "polaris/limit.h"
#include "polaris/provider.h"

int main(int, char**) {
  std::string content =
      "rateLimiter:\n"
      "  rateLimitCluster:\n"
      "    namespace: Polaris\n"
      "    service: polaris.metric.test";
  std::string err_msg;
  polaris::Config* config = polaris::Config::CreateFromString(content, err_msg);
  if (config == NULL) {
    std::cout << "create config with error:" << err_msg << std::endl;
    return -1;
  }

  polaris::Context* context = polaris::Context::Create(config, polaris::kLimitContext);
  assert(context != NULL);
  delete config;

  polaris::LimitApi* limit_api = polaris::LimitApi::Create(context);
  assert(limit_api != NULL);

  polaris::ConsumerApi* consumer_api = polaris::ConsumerApi::Create(context);
  assert(consumer_api != NULL);
  polaris::ProviderApi* provider_api = polaris::ProviderApi::Create(context);
  assert(provider_api != NULL);

  std::cout << "create api success" << std::endl;
  delete consumer_api;
  delete provider_api;
  delete limit_api;
  return 0;
}