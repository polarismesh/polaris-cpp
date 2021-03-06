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

#include "plugin/local_registry/local_registry.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mock/fake_server_response.h"
#include "test_context.h"
#include "test_utils.h"

namespace polaris {

class InMemoryLocalRegistryTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    local_registry_ = new InMemoryRegistry();
    TestUtils::CreateTempDir(persist_dir_);
    std::string content_ = "persistDir:\n  " + persist_dir_;
    std::string err_msg;
    config_ = Config::CreateFromString(content_, err_msg);
    POLARIS_ASSERT(config_ != nullptr && err_msg.empty());
    context_ = TestContext::CreateContext();
    ASSERT_TRUE(context_ != nullptr);
    mock_server_connector_ = TestContext::SetupMockServerConnector(context_);
    ReturnCode ret = local_registry_->Init(config_, context_);
    ASSERT_EQ(ret, kReturnOk);
    service_key_.namespace_ = "service_namespace";
    service_key_.name_ = "service_name";
  }

  virtual void TearDown() {
    if (local_registry_ != nullptr) {
      delete local_registry_;
    }
    if (config_ != nullptr) {
      delete config_;
    }
    mock_server_connector_ = nullptr;
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
    TestUtils::RemoveDir(persist_dir_);
  }

 protected:
  InMemoryRegistry *local_registry_;
  Config *config_;
  Context *context_;
  MockServerConnector *mock_server_connector_;
  ServiceKey service_key_;
  std::string persist_dir_;
};

TEST_F(InMemoryLocalRegistryTest, GetNotExistService) {
  ServiceData *service_data = nullptr;
  ReturnCode ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == nullptr);
  local_registry_->GetServiceDataWithRef(service_key_, kServiceDataRouteRule, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == nullptr);

  // ???????????????????????????
  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == nullptr);
  local_registry_->GetServiceDataWithRef(service_key_, kServiceDataRouteRule, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == nullptr);

  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataRateLimit, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == nullptr);
}

TEST_F(InMemoryLocalRegistryTest, LoadServiceData) {
  // ???????????????????????????????????????
  EXPECT_CALL(*mock_server_connector_,
              RegisterEventHandler(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::Invoke(mock_server_connector_, &MockServerConnector::SaveHandler),
                                 ::testing::Return(kReturnOk)));

  ServiceData *service_data = nullptr;
  ServiceDataNotify *service_notify = nullptr;
  ReturnCode ret =
      local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataInstances, service_data, service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(service_notify != nullptr);
  ASSERT_TRUE(service_data == nullptr);
  // ??????????????????Handler??????NULL????????????????????????
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != nullptr);

  // ?????????handler??????????????????
  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == nullptr);

  ASSERT_FALSE(service_notify->hasData());
  // ??????????????????
  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key_);
  ServiceData *create_service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  ASSERT_TRUE(create_service_data != nullptr);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataInstances, create_service_data);

  // ?????????
  ASSERT_EQ(service_notify->hasData(), true);
  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(service_data != nullptr);

  // ?????????????????????????????????
  ServiceData *disk_service_data = nullptr;
  ServiceDataNotify *got_service_notify = nullptr;
  ret = local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataInstances, disk_service_data,
                                                   got_service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(disk_service_data == nullptr);
  ASSERT_TRUE(got_service_notify != nullptr);
  ASSERT_EQ(got_service_notify, service_notify);

  ServiceData *notify_got_data = nullptr;
  timespec ts = Time::SteadyTimeAdd(0);
  ASSERT_EQ(got_service_notify->WaitDataWithRefUtil(ts, notify_got_data), kReturnOk);
  ASSERT_EQ(notify_got_data, service_data);
  // ?????????????????????notify???????????????????????????????????????????????????notify??????2??????????????????4???
  ASSERT_EQ(notify_got_data->DecrementAndGetRef(), 5);
  ASSERT_EQ(service_data->DecrementAndGetRef(), 4);

  delete mock_server_connector_->saved_handler_;
  mock_server_connector_->saved_handler_ = nullptr;
}

TEST_F(InMemoryLocalRegistryTest, TestUpdateServiceData) {
  EXPECT_CALL(*mock_server_connector_,
              RegisterEventHandler(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::Invoke(mock_server_connector_, &MockServerConnector::SaveHandler),
                                 ::testing::Return(kReturnOk)));
  ServiceData *service_data = nullptr;
  ServiceDataNotify *service_notify = nullptr;
  // ????????????
  ReturnCode ret =
      local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataRouteRule, service_data, service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(service_notify != nullptr);
  ASSERT_FALSE(service_notify->hasData());
  v1::DiscoverResponse response;
  FakeServer::RoutingResponse(response, service_key_);

  // ????????????????????????????????????????????????ServiceData
  ServiceData *init_service_data = ServiceData::CreateFromPb(&response, kDataNotFound);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataRouteRule, init_service_data);
  ASSERT_TRUE(service_notify->hasData());

  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataRouteRule, service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(service_data, init_service_data);
  ASSERT_EQ(service_data->DecrementAndGetRef(), 4);

  // ??????????????????
  ServiceData *new_service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataRouteRule, new_service_data);
  ASSERT_TRUE(service_notify->hasData());
  ASSERT_NE(new_service_data, service_data);

  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataRouteRule, service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(new_service_data, service_data);
  ASSERT_EQ(service_data->DecrementAndGetRef(), 4);

  // ?????????????????????????????????????????????????????????
  ServiceDataNotify *got_service_notify = nullptr;
  ret =
      local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataRouteRule, service_data, got_service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(got_service_notify != nullptr);
  ASSERT_EQ(got_service_notify, service_notify);

  // ????????????????????????
  std::set<ServiceKey> service_key_set;
  ret = local_registry_->GetAllServiceKey(service_key_set);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(service_key_set.size(), 0);

  delete mock_server_connector_->saved_handler_;
  mock_server_connector_->saved_handler_ = nullptr;
}

TEST_F(InMemoryLocalRegistryTest, TestPersistAndLoadSingleService) {
  v1::DiscoverResponse response;
  std::string version = "init_version";
  FakeServer::InstancesResponse(response, service_key_, version);
  for (int i = 0; i < 10; i++) {
    ::v1::Instance *instance = response.mutable_instances()->Add();
    instance->mutable_id()->set_value("instance_" + std::to_string(i));
    instance->mutable_namespace_()->set_value(service_key_.namespace_);
    instance->mutable_service()->set_value(service_key_.name_);
    instance->mutable_host()->set_value("host" + std::to_string(i));
    instance->mutable_port()->set_value(i);
    instance->mutable_weight()->set_value(100);
  }

  EXPECT_CALL(*mock_server_connector_,
              RegisterEventHandler(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::Invoke(mock_server_connector_, &MockServerConnector::SaveHandler),
                                 ::testing::Return(kReturnOk)));
  ServiceData *load_service_data = nullptr;
  ServiceDataNotify *service_notify = nullptr;
  ReturnCode ret = local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataInstances, load_service_data,
                                                              service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(load_service_data == nullptr);
  ASSERT_TRUE(service_notify != nullptr);
  ASSERT_FALSE(service_notify->hasData());
  for (int i = 0; i < 10; ++i) {
    ServiceDataStatus data_status = i % 2 == 0 ? kDataIsSyncing : kDataNotFound;
    ServiceData *create_service_data = ServiceData::CreateFromPb(&response, data_status, i);
    ASSERT_TRUE(create_service_data != nullptr);

    mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataInstances, create_service_data);
    ASSERT_TRUE(service_notify->hasData());
    context_->GetContextImpl()->GetCacheManager()->GetReactor().RunOnce();

    // ??????????????????????????????
    InMemoryRegistry *new_local_registry = new InMemoryRegistry();
    ServiceDataNotify *new_service_notify = nullptr;
    Context *new_context = TestContext::CreateContext();
    ASSERT_TRUE(new_context != nullptr);
    ASSERT_EQ((ret = new_local_registry->Init(config_, new_context)), kReturnOk);

    ret = new_local_registry->GetServiceDataWithRef(service_key_, kServiceDataInstances, load_service_data);
    ASSERT_TRUE(load_service_data == nullptr);
    ASSERT_EQ(ret, kReturnServiceNotFound);
    new_local_registry->LoadServiceDataWithNotify(service_key_, kServiceDataInstances, load_service_data,
                                                  new_service_notify);
    if (data_status == kDataIsSyncing) {  // ????????????
      ASSERT_TRUE(load_service_data->IsAvailable());
      ASSERT_EQ(load_service_data->GetCacheVersion(), 0);
      ASSERT_EQ(load_service_data->GetRevision(), version);
      ASSERT_EQ(load_service_data->GetDataStatus(), kDataInitFromDisk);
      ASSERT_EQ(load_service_data->GetDataType(), kServiceDataInstances);
      ASSERT_EQ(load_service_data->GetServiceKey().namespace_, service_key_.namespace_);
      ASSERT_EQ(load_service_data->GetServiceKey().name_, service_key_.name_);
      ASSERT_EQ(load_service_data->DecrementAndGetRef(), 3);
    }
    delete new_local_registry;
    delete new_context;
  }
  delete mock_server_connector_->saved_handler_;
  mock_server_connector_->saved_handler_ = nullptr;
}

TEST_F(InMemoryLocalRegistryTest, TestServiceExpire) {
  TestUtils::SetUpFakeTime();

  EXPECT_CALL(*mock_server_connector_,
              RegisterEventHandler(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::Invoke(mock_server_connector_, &MockServerConnector::SaveHandler),
                                 ::testing::Return(kReturnOk)));
  ServiceData *service_data = nullptr;
  ServiceDataNotify *service_notify = nullptr;
  ReturnCode ret =
      local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataInstances, service_data, service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(service_data == nullptr);
  ASSERT_TRUE(service_notify != nullptr);
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != nullptr);
  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key_);
  ServiceData *create_service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataInstances, create_service_data);

  // ?????????
  TestUtils::FakeNowIncrement(LocalRegistryConfig::kServiceExpireTimeDefault - 1);
  local_registry_->RemoveExpireServiceData();
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != nullptr);

  EXPECT_CALL(*mock_server_connector_, DeregisterEventHandler(::testing::_, ::testing::_))
      .Times(::testing::Exactly(1))
      .WillRepeatedly(::testing::DoAll(::testing::Invoke(mock_server_connector_, &MockServerConnector::DeleteHandler),
                                       ::testing::Return(kReturnOk)));
  TestUtils::FakeNowIncrement(1);
  local_registry_->RemoveExpireServiceData();
  // ???????????????handler???????????????
  service_data = nullptr;
  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == nullptr);
  ASSERT_TRUE(mock_server_connector_->saved_handler_ == nullptr);

  // ???????????????????????????????????????
  EXPECT_CALL(*mock_server_connector_,
              RegisterEventHandler(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::Invoke(mock_server_connector_, &MockServerConnector::SaveHandler),
                                 ::testing::Return(kReturnOk)));
  service_notify = nullptr;
  ret = local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataInstances, service_data, service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(service_data == nullptr);
  ASSERT_TRUE(service_notify != nullptr);
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != nullptr);
  FakeServer::InstancesResponse(response, service_key_);
  service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  ASSERT_TRUE(service_data != nullptr);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataInstances, service_data);
  ASSERT_TRUE(service_notify->hasData());

  // ?????????
  TestUtils::FakeNowIncrement(LocalRegistryConfig::kServiceExpireTimeDefault - 1);
  local_registry_->RemoveExpireServiceData();
  ServiceData *got_service_data = nullptr;
  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, got_service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(service_data, got_service_data);
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != nullptr);
  ASSERT_EQ(got_service_data->DecrementAndGetRef(), 4);

  // ?????????????????????
  TestUtils::FakeNowIncrement(LocalRegistryConfig::kServiceExpireTimeDefault - 1);
  got_service_data = nullptr;
  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, got_service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(service_data, got_service_data);
  local_registry_->RemoveExpireServiceData();
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != nullptr);
  ASSERT_EQ(got_service_data->DecrementAndGetRef(), 4);

  TestUtils::FakeNowIncrement(LocalRegistryConfig::kServiceExpireTimeDefault - 1);
  local_registry_->RemoveExpireServiceData();
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != nullptr);

  // ????????????
  EXPECT_CALL(*mock_server_connector_, DeregisterEventHandler(::testing::_, ::testing::_))
      .Times(::testing::Exactly(1))
      .WillRepeatedly(::testing::DoAll(::testing::Invoke(mock_server_connector_, &MockServerConnector::DeleteHandler),
                                       ::testing::Return(kReturnOk)));
  TestUtils::FakeNowIncrement(1);
  local_registry_->RemoveExpireServiceData();
  ASSERT_TRUE(mock_server_connector_->saved_handler_ == nullptr);
  TestUtils::TearDownFakeTime();
}

TEST_F(InMemoryLocalRegistryTest, TestOldServiceDataGc) {
  // ????????????
  TestUtils::SetUpFakeTime();
  EXPECT_CALL(*mock_server_connector_,
              RegisterEventHandler(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::Invoke(mock_server_connector_, &MockServerConnector::SaveHandler),
                                 ::testing::Return(kReturnOk)));
  ServiceData *service_data = nullptr;
  ServiceDataNotify *service_notify = nullptr;
  ReturnCode ret =
      local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataInstances, service_data, service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(service_data == nullptr);
  ASSERT_TRUE(service_notify != nullptr);
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != nullptr);
  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key_);
  v1::Service *resp_service = response.mutable_service();
  resp_service->mutable_revision()->set_value("init_version");
  ServiceData *init_service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataInstances, init_service_data);

  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(service_data, init_service_data);        // ?????????????????????????????????????????????2+1
  ASSERT_EQ(service_data->DecrementAndGetRef(), 4);  // ????????????????????????4
  service_data->IncrementRef();                      // ???????????????????????????????????????????????????2+1

  // ????????????????????????
  resp_service->mutable_revision()->set_value("new_version");
  ServiceData *new_service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataInstances, new_service_data);
  service_data = nullptr;
  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(service_data, new_service_data);
  // ??????????????????????????????????????????2?????????????????????notify????????????
  ASSERT_EQ(service_data->DecrementAndGetRef(), 4);

  ASSERT_NE(service_data, init_service_data);  // ??????????????????
  TestUtils::FakeNowIncrement(2000 + 1);
  local_registry_->RunGcTask();
  // ????????????????????????????????????????????????????????????????????????????????????????????????
  ASSERT_EQ(init_service_data->DecrementAndGetRef(), 1);
  delete mock_server_connector_->saved_handler_;
  TestUtils::TearDownFakeTime();
}

}  // namespace polaris
