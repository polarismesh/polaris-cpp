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

#include <gtest/gtest.h>

#include "integration/common/environment.h"
#include "polaris/consumer.h"
#include "polaris/context.h"
#include "polaris/limit.h"
#include "polaris/provider.h"

namespace polaris {

class ContextApiTest : public ::testing::Test {
protected:
  virtual void SetUp() {
    context_ = NULL;
    config_  = NULL;
  }

  virtual void TearDown() {
    if (context_ != NULL) {
      delete context_;
      context_ = NULL;
    }
    if (config_ != NULL) {
      delete config_;
      config_ = NULL;
    }
  }

protected:
  Config *config_;
  Context *context_;
};

TEST_F(ContextApiTest, TestShareContext) {
  std::string err_msg, content;
  config_ = Config::CreateFromString(content, err_msg);
  ASSERT_TRUE(config_ != NULL && err_msg.empty());
  context_ = Context::Create(config_);

  ConsumerApi *consumer = ConsumerApi::Create(context_);
  ASSERT_TRUE(consumer != NULL);
  ProviderApi *provider = ProviderApi::Create(context_);
  ASSERT_TRUE(provider != NULL);
  delete provider;
  delete consumer;
}

TEST_F(ContextApiTest, TestLimitContext) {
  std::string err_msg, content =
                           "global:\n"
                           "  serverConnector:\n"
                           "    addresses: [" +
                           Environment::GetDiscoverServer() +
                           "]\nconsumer:\n"
                           "  localCache:\n"
                           "    persistDir: " +
                           Environment::GetPersistDir() +
                           "\n  circuitBreaker:\n"
                           "    setCircuitBreaker:\n"
                           "      enable: true\n"
                           "rateLimiter:\n"
                           "  rateLimitCluster:\n"
                           "    namespace: Polaris\n"
                           "    service: polaris.metric.test";
  config_ = Config::CreateFromString(content, err_msg);
  ASSERT_TRUE(config_ != NULL && err_msg.empty());
  context_ = Context::Create(config_, kLimitContext);

  LimitApi *limit = LimitApi::Create(context_);
  ASSERT_TRUE(limit != NULL);
  ConsumerApi *consumer = ConsumerApi::Create(context_);
  ASSERT_TRUE(consumer != NULL);
  ProviderApi *provider = ProviderApi::Create(context_);
  ASSERT_TRUE(provider != NULL);
  delete provider;
  delete consumer;
  delete limit;  // 会释放Context
  context_ = NULL;
}

}  // namespace polaris
