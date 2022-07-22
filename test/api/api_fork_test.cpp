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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <pthread.h>

#include "polaris/consumer.h"
#include "polaris/limit.h"
#include "polaris/provider.h"

#include "test_utils.h"

namespace polaris {

struct ProcessArgs {
  Context* context_;
  ConsumerApi* consumer_;
  ProviderApi* provider_;
  LimitApi* limit_;
  ServiceKey service_key_;
  std::string config_content_;
};

class ForApiTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    TestUtils::CreateTempDir(persist_dir_);
    apis_.config_content_ =
        "global:\n"
        "  serverConnector:\n"
        "    addresses:\n"
        "      - 127.0.0.1:" +
        std::to_string(TestUtils::PickUnusedPort()) +
        "\nconsumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        persist_dir_;
    std::string err_msg;
    Config* config = Config::CreateFromString(apis_.config_content_, err_msg);
    ASSERT_TRUE(config != nullptr && err_msg.empty());
    apis_.context_ = Context::Create(config);
    delete config;
    ASSERT_TRUE(apis_.context_ != nullptr);
    ASSERT_TRUE((apis_.consumer_ = ConsumerApi::Create(apis_.context_)) != nullptr);
    ASSERT_TRUE((apis_.provider_ = ProviderApi::Create(apis_.context_)) != nullptr);
    ASSERT_TRUE((apis_.limit_ = LimitApi::Create(apis_.context_)) != nullptr);

    apis_.service_key_.namespace_ = "Test";
    apis_.service_key_.name_ = "test.api.fork";
  }

  virtual void TearDown() {
    if (apis_.consumer_ != nullptr) {
      delete apis_.consumer_;
      apis_.consumer_ = nullptr;
    }
    if (apis_.provider_ != nullptr) {
      delete apis_.provider_;
      apis_.provider_ = nullptr;
    }
    if (apis_.limit_ != nullptr) {
      delete apis_.limit_;
      apis_.limit_ = nullptr;
    }
    if (apis_.context_ != nullptr) {
      delete apis_.context_;
      apis_.context_ = nullptr;
    }
    TestUtils::RemoveDir(persist_dir_);
  }

 protected:
  ProcessArgs apis_;
  std::string persist_dir_;
};

void Process(void* args) {
  ProcessArgs& apis = *static_cast<ProcessArgs*>(args);

  Instance instance;
  GetOneInstanceRequest request(apis.service_key_);
  ASSERT_EQ(apis.consumer_->GetOneInstance(request, instance), kRetrunCallAfterFork);

  InstanceHeartbeatRequest heartbeat(apis.service_key_.namespace_, apis.service_key_.name_, "abcde", "host", 8888);
  ASSERT_EQ(apis.provider_->Heartbeat(heartbeat), kRetrunCallAfterFork);

  QuotaRequest quota_request;
  quota_request.SetServiceNamespace(apis.service_key_.namespace_);
  quota_request.SetServiceName(apis.service_key_.name_);
  QuotaResultCode quota_result;
  ASSERT_EQ(apis.limit_->GetQuota(quota_request, quota_result), kRetrunCallAfterFork);

  // 子进程中创建api对象可以
  ConsumerApi* consumer = ConsumerApi::CreateFromString(apis.config_content_);
  ASSERT_TRUE(consumer != nullptr);
  ASSERT_EQ(consumer->GetOneInstance(request, instance), kReturnTimeout);
  delete consumer;

  // 子进程不要释放父进程的对象
  apis.context_ = nullptr;
  apis.consumer_ = nullptr;
  apis.provider_ = nullptr;
  apis.limit_ = nullptr;
}

TEST_F(ForApiTest, TestFork) {
  Instance instance;
  GetOneInstanceRequest request(apis_.service_key_);
  ASSERT_EQ(apis_.consumer_->GetOneInstance(request, instance), kReturnTimeout);

  InstanceHeartbeatRequest heartbeat(apis_.service_key_.namespace_, apis_.service_key_.name_, "abcde", "host", 8888);
  ASSERT_EQ(apis_.provider_->Heartbeat(heartbeat), kReturnNetworkFailed);

  QuotaRequest quota_request;
  quota_request.SetServiceNamespace(apis_.service_key_.namespace_);
  quota_request.SetServiceName(apis_.service_key_.name_);
  QuotaResultCode quota_result;
  ASSERT_EQ(apis_.limit_->GetQuota(quota_request, quota_result), kReturnTimeout);

  if (fork() == 0) {
    Process(&apis_);
    return;
  }

  ASSERT_EQ(apis_.consumer_->GetOneInstance(request, instance), kReturnTimeout);
  ASSERT_EQ(apis_.provider_->Heartbeat(heartbeat), kReturnNetworkFailed);
  ASSERT_EQ(apis_.limit_->GetQuota(quota_request, quota_result), kReturnTimeout);
}

}  // namespace polaris