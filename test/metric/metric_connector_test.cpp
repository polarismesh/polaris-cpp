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

#include "metric/metric_connector.h"

#include <gtest/gtest.h>

#include "test_context.h"
#include "test_utils.h"
#include "v1/code.pb.h"

namespace polaris {

class MetricConnectorForTest : public MetricConnector {
 public:
  MetricConnectorForTest(Reactor& reactor, Context* context) : MetricConnector(reactor, context) {}

  std::map<std::string, MetricConnection*>& GetConnectionMgr() { return connection_mgr_; }

  static void ConnectionIdleCheck(MetricConnectorForTest* connector) {
    MetricConnector::ConnectionIdleCheck(connector);
  }

 protected:
  virtual ReturnCode SelectInstance(const std::string& hash_key, Instance** instance) {
    if (hash_key == ":") {
      return kReturnTimeout;
    }
    *instance = new Instance(hash_key, "127.0.0.1", 8081, 100);
    return kReturnOk;
  }
};

class MetricRequestCallbackForTest : public grpc::RpcCallback<v1::MetricResponse> {
 public:
  explicit MetricRequestCallbackForTest(bool result, int called = 1)
      : result_(result), called_(called), real_called_(0) {}

  virtual ~MetricRequestCallbackForTest() { EXPECT_EQ(real_called_, called_); }

  virtual void OnSuccess(v1::MetricResponse* response) {
    ASSERT_EQ(result_, true);
    delete response;
    real_called_++;
  }

  virtual void OnError(ReturnCode ret_code) {
    ASSERT_EQ(result_, false);
    ASSERT_NE(ret_code, kReturnOk);
    real_called_++;
  }

 private:
  bool result_;
  int called_;
  int real_called_;
};

class MetricConnectorTest : public ::testing::Test {
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    connector_ = new MetricConnectorForTest(reactor_, context_);
    service_key_.namespace_ = "test";
    service_key_.name_ = "metric";
    hash_key_ = service_key_.namespace_ + ":" + service_key_.name_;
    msg_id_ = 123456;
    metric_key_.set_namespace_(service_key_.namespace_);
    metric_key_.set_service(service_key_.name_);
  }

  virtual void TearDown() {
    reactor_.Stop();
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
    if (connector_ != nullptr) {
      delete connector_;
      connector_ = nullptr;
    }
  }

 protected:
  Reactor reactor_;
  Context* context_;
  MetricConnectorForTest* connector_;
  ServiceKey service_key_;
  std::string hash_key_;
  int64_t msg_id_;
  v1::MetricKey metric_key_;
};

TEST_F(MetricConnectorTest, SelectConnectionError) {
  connector_->Initialize(new v1::MetricInitRequest(), 1000, new MetricRequestCallbackForTest(false));
  connector_->Report(new v1::MetricRequest(), 1000, new MetricRequestCallbackForTest(false));
  connector_->Query(new v1::MetricQueryRequest(), 1000, new MetricRequestCallbackForTest(false));
}

TEST_F(MetricConnectorTest, ConnectionFailed) {
  v1::MetricInitRequest* init_request = new v1::MetricInitRequest();
  init_request->mutable_key()->set_namespace_(service_key_.namespace_);
  init_request->mutable_key()->set_service(service_key_.name_);
  connector_->Initialize(init_request, 1000, new MetricRequestCallbackForTest(false));
  ASSERT_TRUE(connector_->GetConnectionMgr().count(hash_key_) == 1);
  MetricConnection* connection = connector_->GetConnectionMgr()[hash_key_];
  connection->OnConnect(kReturnNetworkFailed);
  ASSERT_TRUE(connector_->GetConnectionMgr().count(hash_key_) == 0);
  ASSERT_FALSE(connector_->IsMetricInit(&metric_key_));
}

TEST_F(MetricConnectorTest, MetricInitRequestFailed) {
  v1::MetricInitRequest* init_request = new v1::MetricInitRequest();
  init_request->mutable_key()->set_namespace_(service_key_.namespace_);
  init_request->mutable_key()->set_service(service_key_.name_);
  connector_->Initialize(init_request, 1000, new MetricRequestCallbackForTest(false));
  ASSERT_TRUE(connector_->GetConnectionMgr().count(hash_key_) == 1);
  MetricConnection* connection = connector_->GetConnectionMgr()[hash_key_];
  connection->OnConnect(kReturnOk);
  connection->OnFailure("unavailable");
  ASSERT_TRUE(connector_->GetConnectionMgr().count(hash_key_) == 0);
  ASSERT_FALSE(connector_->IsMetricInit(&metric_key_));
}

TEST_F(MetricConnectorTest, MetricInitSuccess) {
  v1::MetricInitRequest* init_request = new v1::MetricInitRequest();
  init_request->mutable_msgid()->set_value(msg_id_);
  init_request->mutable_key()->set_namespace_(service_key_.namespace_);
  init_request->mutable_key()->set_service(service_key_.name_);
  connector_->Initialize(init_request, 1000, new MetricRequestCallbackForTest(true));
  ASSERT_TRUE(connector_->GetConnectionMgr().count(hash_key_) == 1);
  MetricConnection* connection = connector_->GetConnectionMgr()[hash_key_];
  connection->OnConnect(kReturnOk);
  connection->OnSuccess(new v1::MetricResponse());  // 无效应答
  v1::MetricResponse* response = new v1::MetricResponse();
  response->mutable_msgid()->set_value(msg_id_);
  response->mutable_code()->set_value(v1::ExecuteSuccess);
  connection->OnSuccess(response);
  ASSERT_TRUE(connector_->IsMetricInit(&metric_key_));
}

TEST_F(MetricConnectorTest, MetricReport) {
  v1::MetricInitRequest* init_request = new v1::MetricInitRequest();
  init_request->mutable_msgid()->set_value(msg_id_);
  init_request->mutable_key()->set_namespace_(service_key_.namespace_);
  init_request->mutable_key()->set_service(service_key_.name_);
  connector_->Initialize(init_request, 1000, new MetricRequestCallbackForTest(true));
  ASSERT_TRUE(connector_->GetConnectionMgr().count(hash_key_) == 1);
  MetricConnection* connection = connector_->GetConnectionMgr()[hash_key_];
  connection->OnConnect(kReturnOk);
  v1::MetricResponse* response = new v1::MetricResponse();
  response->mutable_code()->set_value(v1::ExecuteSuccess);
  response->mutable_msgid()->set_value(msg_id_);
  connection->OnSuccess(response);
  ASSERT_TRUE(connector_->IsMetricInit(&metric_key_));
  for (int i = 0; i < 10; ++i) {
    v1::MetricRequest* request = new v1::MetricRequest();
    request->mutable_msgid()->set_value(i);
    request->mutable_key()->set_namespace_(service_key_.namespace_);
    request->mutable_key()->set_service(service_key_.name_);
    if (i != 9) {
      connector_->Report(request, 1000, new MetricRequestCallbackForTest(true));
      v1::MetricResponse* response = new v1::MetricResponse();
      response->mutable_msgid()->set_value(i);
      connection->OnReceiveMessage(response);
    } else {
      connector_->Report(request, 1000, new MetricRequestCallbackForTest(false));
      v1::MetricResponse* response = new v1::MetricResponse();
      connection->OnReceiveMessage(response);
      connection->OnRemoteClose("unavailable");
    }
  }
}

TEST_F(MetricConnectorTest, CheckIdleConnection) {
  TestUtils::SetUpFakeTime();
  v1::MetricInitRequest* init_request = new v1::MetricInitRequest();
  init_request->mutable_msgid()->set_value(msg_id_);
  init_request->mutable_key()->set_namespace_(service_key_.namespace_);
  init_request->mutable_key()->set_service(service_key_.name_);
  connector_->Initialize(init_request, 1000, new MetricRequestCallbackForTest(true));
  ASSERT_TRUE(connector_->GetConnectionMgr().count(hash_key_) == 1);
  MetricConnection* connection = connector_->GetConnectionMgr()[hash_key_];
  connection->OnConnect(kReturnOk);
  v1::MetricResponse* response = new v1::MetricResponse();
  response->mutable_msgid()->set_value(msg_id_);
  response->mutable_code()->set_value(v1::ExecuteSuccess);
  connection->OnSuccess(response);
  ASSERT_TRUE(connector_->IsMetricInit(&metric_key_));
  ASSERT_EQ(connector_->GetConnectionMgr().count(hash_key_), 1);
  TestUtils::FakeNowIncrement(30 * 1000);
  MetricConnectorForTest::ConnectionIdleCheck(connector_);
  ASSERT_EQ(connector_->GetConnectionMgr().count(hash_key_), 1);

  // 触发使用一次连接
  v1::MetricRequest* request = new v1::MetricRequest();
  request->mutable_key()->set_namespace_(service_key_.namespace_);
  request->mutable_key()->set_service(service_key_.name_);
  request->mutable_msgid()->set_value(msg_id_ + 1);
  connector_->Report(request, 1000, new MetricRequestCallbackForTest(true));
  response = new v1::MetricResponse();
  response->mutable_msgid()->set_value(msg_id_ + 1);
  connection->OnSuccess(response);

  TestUtils::FakeNowIncrement(31 * 1000);
  MetricConnectorForTest::ConnectionIdleCheck(connector_);
  ASSERT_EQ(connector_->GetConnectionMgr().count(hash_key_), 1);
  ASSERT_FALSE(connector_->IsMetricInit(&metric_key_));  // metric_key的Init请求过期了

  TestUtils::FakeNowIncrement(31 * 1000);
  MetricConnectorForTest::ConnectionIdleCheck(connector_);
  ASSERT_EQ(connector_->GetConnectionMgr().count(hash_key_), 0);  // 连接过期删除了
  TestUtils::TearDownFakeTime();
}

TEST_F(MetricConnectorTest, MetricQuery) {
  v1::MetricQueryRequest* query_request = new v1::MetricQueryRequest();
  query_request->mutable_msgid()->set_value(msg_id_);
  query_request->mutable_key()->set_namespace_(service_key_.namespace_);
  query_request->mutable_key()->set_service(service_key_.name_);
  connector_->Query(query_request, 1000, new MetricRequestCallbackForTest(true));
  ASSERT_TRUE(connector_->GetConnectionMgr().count(hash_key_) == 1);
  MetricConnection* connection = connector_->GetConnectionMgr()[hash_key_];
  connection->OnConnect(kReturnOk);
  v1::MetricResponse* response = new v1::MetricResponse();
  response->mutable_msgid()->set_value(msg_id_);
  connection->OnReceiveMessage(response);
  for (int i = 0; i < 10; ++i) {
    v1::MetricQueryRequest* query_request = new v1::MetricQueryRequest();
    query_request->mutable_msgid()->set_value(i);
    query_request->mutable_key()->set_namespace_(service_key_.namespace_);
    query_request->mutable_key()->set_service(service_key_.name_);
    if (i != 9) {
      connector_->Query(query_request, 1000, new MetricRequestCallbackForTest(true));
      v1::MetricResponse* response = new v1::MetricResponse();
      response->mutable_msgid()->set_value(i);
      connection->OnReceiveMessage(response);
    } else {
      connector_->Query(query_request, 1000, new MetricRequestCallbackForTest(false));
      v1::MetricResponse* response = new v1::MetricResponse();
      connection->OnReceiveMessage(response);
      connection->OnRemoteClose("unavailable");
    }
  }
}

}  // namespace polaris
