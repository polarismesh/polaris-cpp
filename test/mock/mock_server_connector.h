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

#ifndef POLARIS_CPP_TEST_MOCK_MOCK_SERVER_CONNECTOR_H_
#define POLARIS_CPP_TEST_MOCK_MOCK_SERVER_CONNECTOR_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <pthread.h>

#include <mutex>
#include <string>
#include <vector>

#include "context/context_impl.h"
#include "plugin/server_connector/server_connector.h"
#include "polaris/plugin.h"
#include "polaris/provider.h"

namespace polaris {

class MockServerConnector : public ServerConnector {
 public:
  MockServerConnector() : saved_handler_(nullptr) {}

  MOCK_METHOD2(Init, ReturnCode(Config *config, Context *context));

  MOCK_METHOD5(RegisterEventHandler,
               ReturnCode(const ServiceKey &service_key, ServiceDataType data_type, uint64_t sync_interval,
                          const std::string &disk_revision, ServiceEventHandler *handler));

  MOCK_METHOD2(DeregisterEventHandler, ReturnCode(const ServiceKey &service_key, ServiceDataType data_type));

  MOCK_METHOD3(RegisterInstance,
               ReturnCode(const InstanceRegisterRequest &req, uint64_t timeout_ms, std::string &instance_id));

  MOCK_METHOD2(DeregisterInstance, ReturnCode(const InstanceDeregisterRequest &req, uint64_t timeout_ms));

  MOCK_METHOD2(InstanceHeartbeat, ReturnCode(const InstanceHeartbeatRequest &req, uint64_t timeout_ms));

  MOCK_METHOD3(AsyncInstanceHeartbeat,
               ReturnCode(const InstanceHeartbeatRequest &req, uint64_t timeout_ms, ProviderCallback *callback));

  MOCK_METHOD3(AsyncReportClient, ReturnCode(const std::string &host, uint64_t timeout_ms, PolarisCallback callback));

  void SaveHandler(const ServiceKey & /*service_key*/, ServiceDataType /*data_type*/, uint64_t /*sync_interval*/,
                   const std::string & /*disk_revision*/, ServiceEventHandler *handler) {
    ASSERT_TRUE(handler != nullptr);
    saved_handler_ = handler;
  }
  ServiceEventHandler *saved_handler_;

  void DeleteHandler(const ServiceKey &service_key, ServiceDataType data_type) {
    ASSERT_TRUE(saved_handler_ != nullptr);
    saved_handler_->OnEventUpdate(service_key, data_type, nullptr);
    delete saved_handler_;
    saved_handler_ = nullptr;
  }
};

struct EventHandlerData {
  ServiceKey service_key_;
  ServiceDataType data_type_;
  ServiceData *service_data_;
  ServiceEventHandler *handler_;
};

class MockServerConnectorTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    server_connector_ = new MockServerConnector();
    server_connector_plugin_name_ = "mock";
    ReturnCode ret = RegisterPlugin(server_connector_plugin_name_, kPluginServerConnector, MockServerConnectorFactory);
    ASSERT_EQ(ret, kReturnOk);
    EXPECT_CALL(*server_connector_, Init(testing::_, testing::_)).WillOnce(::testing::Return(kReturnOk));
    EXPECT_CALL(*server_connector_, AsyncReportClient(::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Return(kReturnOk));
    // 12次分别对应4个内部服务
    EXPECT_CALL(*server_connector_,
                RegisterEventHandler(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(::testing::AtMost(12))
        .WillRepeatedly(::testing::DoAll(::testing::Invoke(this, &MockServerConnectorTest::MockIgnoreEventHandler),
                                         ::testing::Return(kReturnOk)));
  }

  virtual void TearDown() {
    for (std::size_t i = 0; i < handler_list_.size(); ++i) {
      delete handler_list_[i];
    }
    handler_list_.clear();
  }

 public:
  void MockIgnoreEventHandler(const ServiceKey & /*service_key*/, ServiceDataType /*data_type*/,
                              uint64_t /*sync_interval*/, const std::string & /*disk_revision*/,
                              ServiceEventHandler *handler) {
    ASSERT_TRUE(handler != nullptr);
    const std::lock_guard<std::mutex> mutex_guard(handler_lock_);
    handler_list_.push_back(handler);
  }

  static void *AsyncEventUpdate(void *args) {
    EventHandlerData *event_data = static_cast<EventHandlerData *>(args);
    EXPECT_TRUE(event_data->handler_ != nullptr);
    event_data->handler_->OnEventUpdate(event_data->service_key_, event_data->data_type_, event_data->service_data_);
    delete event_data;
    return nullptr;
  }

 protected:
  static Plugin *MockServerConnectorFactory() { return server_connector_; }
  static MockServerConnector *server_connector_;
  std::string server_connector_plugin_name_;
  std::mutex handler_lock_;
  std::vector<ServiceEventHandler *> handler_list_;
};

MockServerConnector *MockServerConnectorTest::server_connector_ = nullptr;

class TestProviderCallback : public ProviderCallback {
 public:
  TestProviderCallback(ReturnCode ret_code, int line) : ret_code_(ret_code), line_(line) {}

  ~TestProviderCallback() {}

  virtual void Response(ReturnCode code, const std::string &) {
    ASSERT_EQ(code, ret_code_) << "failed line: " << line_;
  }

 private:
  ReturnCode ret_code_;
  int line_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_MOCK_MOCK_SERVER_CONNECTOR_H_
