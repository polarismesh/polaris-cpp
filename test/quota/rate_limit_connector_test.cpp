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

#include "quota/rate_limit_connector.h"

#include <gtest/gtest.h>

#include "quota/rate_limit_window.h"
#include "test_context.h"
#include "test_utils.h"
#include "v1/code.pb.h"

namespace polaris {

class RateLimitConnectorForTest : public RateLimitConnector {
public:
  RateLimitConnectorForTest(Reactor& reactor, Context* context)
      : RateLimitConnector(reactor, context, 1000), server_host_("127.0.0.1") {}

  std::map<std::string, RateLimitConnection*>& GetConnectionMgr() { return connection_mgr_; }

protected:
  virtual ReturnCode SelectInstance(const ServiceKey&, const std::string& hash_key,
                                    Instance** instance) {
    if (hash_key.empty()) {
      return kReturnTimeout;
    }
    if (server_host_.empty()) {
      return kReturnInstanceNotFound;
    }
    *instance = new Instance(hash_key, server_host_, 8081, 100);
    return kReturnOk;
  }

public:
  std::string server_host_;
};

class RateLimitConnectorTest : public ::testing::Test {
  virtual void SetUp() {
    context_   = TestContext::CreateContext();
    connector_ = new RateLimitConnectorForTest(reactor_, context_);
    RateLimitWindowKey window_key;
    window_ = new RateLimitWindow(reactor_, NULL, window_key);
    v1::Rule rule;
    rule.set_type(v1::Rule::LOCAL);
    v1::Amount* amount = rule.add_amounts();
    amount->mutable_maxamount()->set_value(10);
    amount->mutable_validduration()->set_seconds(1);
    rule.mutable_namespace_()->set_value("Test");
    rule.mutable_service()->set_value("service");
    rule.mutable_id()->set_value("id123");
    ASSERT_TRUE(rate_limit_rule_.Init(rule));
    ASSERT_EQ(window_->Init(NULL, &rate_limit_rule_, rate_limit_rule_.GetId(), connector_),
              kReturnOk);
    connection_id_ = "127.0.0.1:8081";
  }

  virtual void TearDown() {
    reactor_.Stop();
    if (context_ != NULL) {
      delete context_;
      context_ = NULL;
    }
    if (connector_ != NULL) {
      delete connector_;
      connector_ = NULL;
    }
    if (window_ != NULL) {
      window_->DecrementRef();
      window_ = NULL;
    }
  }

protected:
  Reactor reactor_;
  Context* context_;
  RateLimitConnectorForTest* connector_;
  RateLimitRule rate_limit_rule_;
  RateLimitWindow* window_;
  std::string connection_id_;
};

TEST_F(RateLimitConnectorTest, ConnectionFailed) {
  connector_->SyncTask(window_);
  RateLimitConnection* connection = connector_->GetConnectionMgr()[connection_id_];
  ASSERT_TRUE(connection != NULL);
  reactor_.RunOnce();
  connection->OnConnectFailed();
}

TEST_F(RateLimitConnectorTest, ConnectionInitFailed) {
  connector_->SyncTask(window_);
  RateLimitConnection* connection = connector_->GetConnectionMgr()[connection_id_];
  ASSERT_TRUE(connection != NULL);
  reactor_.RunOnce();
  connection->OnConnectSuccess();
  connection->OnRemoteClose(grpc::kGrpcStatusUnavailable, "unavailable");
}

TEST_F(RateLimitConnectorTest, ConnectionInit) {
  connector_->SyncTask(window_);
  RateLimitConnection* connection = connector_->GetConnectionMgr()[connection_id_];
  ASSERT_TRUE(connection != NULL);
  connection->OnConnectSuccess();
  metric::v2::RateLimitResponse* response = new metric::v2::RateLimitResponse();
  connection->OnReceiveMessage(response);
  response = new metric::v2::RateLimitResponse();
  response->set_cmd(metric::v2::INIT);
  metric::v2::RateLimitInitResponse* init_response = response->mutable_ratelimitinitresponse();
  init_response->set_code(v1::ExecuteSuccess);
  init_response->set_timestamp(Time::GetCurrentTimeMs());
  init_response->set_clientkey(12);
  init_response->mutable_target()->set_namespace_("Test");
  init_response->mutable_target()->set_service("service");
  metric::v2::QuotaCounter* counter = init_response->add_counters();
  counter->set_left(10);
  counter->set_duration(1);
  counter->set_clientcount(1);
  connection->OnReceiveMessage(response);
}

TEST_F(RateLimitConnectorTest, CheckIdleConnection) {
  connector_->SyncTask(window_);
  RateLimitConnection* connection = connector_->GetConnectionMgr()[connection_id_];
  ASSERT_TRUE(connection != NULL);
  ASSERT_EQ(connector_->GetConnectionMgr().size(), 1);
  TestUtils::SetUpFakeTime();
  metric::v2::RateLimitResponse* response = new metric::v2::RateLimitResponse();
  connection->OnReceiveMessage(response);
  ASSERT_EQ(connector_->GetConnectionMgr().count(connection_id_), 1);
  TestUtils::FakeNowIncrement(10 * 1000);
  RateLimitConnector::ConnectionIdleCheck(connector_);
  ASSERT_EQ(connector_->GetConnectionMgr().count(connection_id_), 1);
  TestUtils::FakeNowIncrement(60 * 1000);
  RateLimitConnector::ConnectionIdleCheck(connector_);
  ASSERT_EQ(connector_->GetConnectionMgr().count(connection_id_), 0);
  TestUtils::TearDownFakeTime();
}

// 测试window连接切换的场景
TEST_F(RateLimitConnectorTest, WindowReconnect) {
  connector_->SyncTask(window_);
  RateLimitConnection* connection = connector_->GetConnectionMgr()[connection_id_];
  ASSERT_TRUE(connection != NULL);
  ASSERT_EQ(connection_id_, window_->GetConnectionId());
  connection->OnConnectSuccess();
  metric::v2::RateLimitResponse* response = new metric::v2::RateLimitResponse();
  connection->OnReceiveMessage(response);

  connector_->server_host_ = "127.0.0.2";
  connector_->SyncTask(window_);
  ASSERT_EQ("127.0.0.2:8081", window_->GetConnectionId());

  connector_->server_host_ = "127.0.0.1";
  connector_->SyncTask(window_);
  ASSERT_EQ(connection_id_, window_->GetConnectionId());
}

// window连接切换后无可用连接
TEST_F(RateLimitConnectorTest, WindowReconnectWithNoInstance) {
  connector_->SyncTask(window_);
  RateLimitConnection* connection = connector_->GetConnectionMgr()[connection_id_];
  ASSERT_TRUE(connection != NULL);
  ASSERT_EQ(connection_id_, window_->GetConnectionId());

  connector_->server_host_ = "";
  connector_->SyncTask(window_);
  ASSERT_EQ("127.0.0.1:8081", window_->GetConnectionId());  // 连接ID不变更

  connector_->server_host_ = "127.0.0.2";  // 又可用了以后连接ID变更
  connector_->SyncTask(window_);
  ASSERT_EQ("127.0.0.2:8081", window_->GetConnectionId());
}

}  // namespace polaris
