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
    POLARIS_ASSERT(config_ != NULL && err_msg.empty());
    context_ = TestContext::CreateContext();
    ASSERT_TRUE(context_ != NULL);
    mock_server_connector_ = TestContext::SetupMockServerConnector(context_);
    ReturnCode ret         = local_registry_->Init(config_, context_);
    ASSERT_EQ(ret, kReturnOk);
    service_key_.namespace_ = "service_namespace";
    service_key_.name_      = "service_name";
  }

  virtual void TearDown() {
    if (local_registry_ != NULL) {
      delete local_registry_;
    }
    if (config_ != NULL) {
      delete config_;
    }
    mock_server_connector_ = NULL;
    if (context_ != NULL) {
      delete context_;
      context_ = NULL;
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
  ServiceData *service_data = NULL;
  ReturnCode ret =
      local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == NULL);
  local_registry_->GetServiceDataWithRef(service_key_, kServiceDataRouteRule, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == NULL);

  // 重复获取依然找不到
  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == NULL);
  local_registry_->GetServiceDataWithRef(service_key_, kServiceDataRouteRule, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == NULL);

  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataRateLimit, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == NULL);
}

TEST_F(InMemoryLocalRegistryTest, LoadServiceData) {
  // 同服务，同类型只会注册一次
  EXPECT_CALL(*mock_server_connector_,
              RegisterEventHandler(::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(
          ::testing::Invoke(mock_server_connector_, &MockServerConnector::SaveHandler),
          ::testing::Return(kReturnOk)));

  ServiceData *service_data         = NULL;
  ServiceDataNotify *service_notify = NULL;
  ReturnCode ret = local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataInstances,
                                                              service_data, service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(service_notify != NULL);
  ASSERT_TRUE(service_data == NULL);
  // 保存的注册的Handler不为NULL，表示已经注册了
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != NULL);

  // 未触发handler前依然无数据
  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == NULL);

  ASSERT_FALSE(service_notify->hasData());
  // 触发数据更新
  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key_);
  ServiceData *create_service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  ASSERT_TRUE(create_service_data != NULL);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataInstances,
                                                        create_service_data);

  // 有数据
  ASSERT_EQ(service_notify->hasData(), true);
  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(service_data != NULL);

  // 获取的是同一个通知对象
  ServiceData *disk_service_data        = NULL;
  ServiceDataNotify *got_service_notify = NULL;
  ret = local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataInstances,
                                                   disk_service_data, got_service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(disk_service_data == NULL);
  ASSERT_TRUE(got_service_notify != NULL);
  ASSERT_EQ(got_service_notify, service_notify);

  ServiceData *notify_got_data = NULL;
  timespec ts                  = Time::CurrentTimeAddWith(0);
  ASSERT_EQ(got_service_notify->WaitDataWithRefUtil(ts, notify_got_data), kReturnOk);
  ASSERT_EQ(notify_got_data, service_data);
  // 从缓存服务里和notify里各获取了一次服务引用，加上缓存和notify本身2次引用，总共4次
  ASSERT_EQ(notify_got_data->DecrementAndGetRef(), 4);
  ASSERT_EQ(service_data->DecrementAndGetRef(), 3);

  delete mock_server_connector_->saved_handler_;
  mock_server_connector_->saved_handler_ = NULL;
}

TEST_F(InMemoryLocalRegistryTest, TestUpdateServiceData) {
  EXPECT_CALL(*mock_server_connector_,
              RegisterEventHandler(::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(
          ::testing::Invoke(mock_server_connector_, &MockServerConnector::SaveHandler),
          ::testing::Return(kReturnOk)));
  ServiceData *service_data         = NULL;
  ServiceDataNotify *service_notify = NULL;
  // 加载服务
  ReturnCode ret = local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataRouteRule,
                                                              service_data, service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(service_notify != NULL);
  ASSERT_FALSE(service_notify->hasData());
  v1::DiscoverResponse response;
  FakeServer::RoutingResponse(response, service_key_);
  ServiceData *init_service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataRouteRule,
                                                        init_service_data);
  ASSERT_TRUE(service_notify->hasData());

  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataRouteRule, service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(service_data, init_service_data);
  ASSERT_EQ(service_data->DecrementAndGetRef(), 3);

  // 再次更新数据
  ServiceData *new_service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataRouteRule,
                                                        new_service_data);
  ASSERT_TRUE(service_notify->hasData());
  ASSERT_NE(new_service_data, service_data);

  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataRouteRule, service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(new_service_data, service_data);
  ASSERT_EQ(service_data->DecrementAndGetRef(), 3);

  // 服务加载通知不会随着服务数据更新而改变
  ServiceDataNotify *got_service_notify = NULL;
  ret = local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataRouteRule,
                                                   service_data, got_service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(got_service_notify != NULL);
  ASSERT_EQ(got_service_notify, service_notify);

  // 测试获取服务列表
  std::set<ServiceKey> service_key_set;
  ret = local_registry_->GetAllServiceKey(service_key_set);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(service_key_set.size(), 0);

  delete mock_server_connector_->saved_handler_;
  mock_server_connector_->saved_handler_ = NULL;
}

TEST_F(InMemoryLocalRegistryTest, TestPersistAndLoadSingleService) {
  v1::DiscoverResponse response;
  std::string version = "init_version";
  FakeServer::InstancesResponse(response, service_key_, version);
  for (int i = 0; i < 10; i++) {
    ::v1::Instance *instance = response.mutable_instances()->Add();
    instance->mutable_id()->set_value("instance_" + StringUtils::TypeToStr<int>(i));
    instance->mutable_namespace_()->set_value(service_key_.namespace_);
    instance->mutable_service()->set_value(service_key_.name_);
    instance->mutable_host()->set_value("host" + StringUtils::TypeToStr<int>(i));
    instance->mutable_port()->set_value(i);
    instance->mutable_weight()->set_value(100);
  }

  EXPECT_CALL(*mock_server_connector_,
              RegisterEventHandler(::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(
          ::testing::Invoke(mock_server_connector_, &MockServerConnector::SaveHandler),
          ::testing::Return(kReturnOk)));
  ServiceData *load_service_data    = NULL;
  ServiceDataNotify *service_notify = NULL;
  ReturnCode ret = local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataInstances,
                                                              load_service_data, service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(load_service_data == NULL);
  ASSERT_TRUE(service_notify != NULL);
  ASSERT_FALSE(service_notify->hasData());
  for (int i = 0; i < 10; ++i) {
    ServiceDataStatus data_status    = i % 2 == 0 ? kDataIsSyncing : kDataNotFound;
    ServiceData *create_service_data = ServiceData::CreateFromPb(&response, data_status, i);
    ASSERT_TRUE(create_service_data != NULL);

    mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataInstances,
                                                          create_service_data);
    ASSERT_TRUE(service_notify->hasData());
    context_->GetContextImpl()->GetCacheManager()->GetReactor().RunOnce();

    // 创建另一个对象来加载
    InMemoryRegistry *new_local_registry  = new InMemoryRegistry();
    ServiceDataNotify *new_service_notify = NULL;
    Context *new_context                  = TestContext::CreateContext();
    ASSERT_TRUE(new_context != NULL);
    ASSERT_EQ((ret = new_local_registry->Init(config_, new_context)), kReturnOk);

    ret = new_local_registry->GetServiceDataWithRef(service_key_, kServiceDataInstances,
                                                    load_service_data);
    ASSERT_TRUE(load_service_data == NULL);
    ASSERT_EQ(ret, kReturnServiceNotFound);
    new_local_registry->LoadServiceDataWithNotify(service_key_, kServiceDataInstances,
                                                  load_service_data, new_service_notify);
    if (data_status == kDataIsSyncing) {  // 校验数据
      ASSERT_TRUE(load_service_data->IsAvailable());
      ASSERT_EQ(load_service_data->GetCacheVersion(), 0);
      ASSERT_EQ(load_service_data->GetRevision(), version);
      ASSERT_EQ(load_service_data->GetDataStatus(), kDataInitFromDisk);
      ASSERT_EQ(load_service_data->GetDataType(), kServiceDataInstances);
      ASSERT_EQ(load_service_data->GetServiceKey().namespace_, service_key_.namespace_);
      ASSERT_EQ(load_service_data->GetServiceKey().name_, service_key_.name_);
      ASSERT_EQ(load_service_data->DecrementAndGetRef(), 1);
    }
    delete new_local_registry;
    delete new_context;
  }
  delete mock_server_connector_->saved_handler_;
  mock_server_connector_->saved_handler_ = NULL;
}

TEST_F(InMemoryLocalRegistryTest, TestServiceExpire) {
  TestUtils::SetUpFakeTime();

  EXPECT_CALL(*mock_server_connector_,
              RegisterEventHandler(::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(
          ::testing::Invoke(mock_server_connector_, &MockServerConnector::SaveHandler),
          ::testing::Return(kReturnOk)));
  ServiceData *service_data         = NULL;
  ServiceDataNotify *service_notify = NULL;
  ReturnCode ret = local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataInstances,
                                                              service_data, service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(service_data == NULL);
  ASSERT_TRUE(service_notify != NULL);
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != NULL);
  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key_);
  ServiceData *create_service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataInstances,
                                                        create_service_data);

  // 未过期
  TestUtils::FakeNowIncrement(LocalRegistryConfig::kServiceExpireTimeDefault - 1);
  local_registry_->RemoveExpireServiceData(Time::GetCurrentTimeMs());
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != NULL);

  EXPECT_CALL(*mock_server_connector_, DeregisterEventHandler(::testing::_, ::testing::_))
      .Times(::testing::Exactly(1))
      .WillRepeatedly(::testing::DoAll(
          ::testing::Invoke(mock_server_connector_, &MockServerConnector::DeleteHandler),
          ::testing::Return(kReturnOk)));
  TestUtils::FakeNowIncrement(1);
  local_registry_->RemoveExpireServiceData(Time::GetCurrentTimeMs());
  // 服务过期，handler已经被删除
  service_data = NULL;
  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnServiceNotFound);
  ASSERT_TRUE(service_data == NULL);
  ASSERT_TRUE(mock_server_connector_->saved_handler_ == NULL);

  // 验证服务访问会更新超时时间
  EXPECT_CALL(*mock_server_connector_,
              RegisterEventHandler(::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(
          ::testing::Invoke(mock_server_connector_, &MockServerConnector::SaveHandler),
          ::testing::Return(kReturnOk)));
  service_notify = NULL;
  ret            = local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataInstances,
                                                   service_data, service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(service_data == NULL);
  ASSERT_TRUE(service_notify != NULL);
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != NULL);
  FakeServer::InstancesResponse(response, service_key_);
  service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  ASSERT_TRUE(service_data != NULL);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataInstances,
                                                        service_data);
  ASSERT_TRUE(service_notify->hasData());

  // 未过期
  TestUtils::FakeNowIncrement(LocalRegistryConfig::kServiceExpireTimeDefault - 1);
  local_registry_->RemoveExpireServiceData(Time::GetCurrentTimeMs());
  ServiceData *got_service_data = NULL;
  ret =
      local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, got_service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(service_data, got_service_data);
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != NULL);
  ASSERT_EQ(got_service_data->DecrementAndGetRef(), 3);

  // 访问会更新时间
  TestUtils::FakeNowIncrement(LocalRegistryConfig::kServiceExpireTimeDefault - 1);
  got_service_data = NULL;
  ret =
      local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, got_service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(service_data, got_service_data);
  local_registry_->RemoveExpireServiceData(Time::GetCurrentTimeMs());
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != NULL);
  ASSERT_EQ(got_service_data->DecrementAndGetRef(), 3);

  TestUtils::FakeNowIncrement(LocalRegistryConfig::kServiceExpireTimeDefault - 1);
  local_registry_->RemoveExpireServiceData(Time::GetCurrentTimeMs());
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != NULL);

  // 服务过期
  EXPECT_CALL(*mock_server_connector_, DeregisterEventHandler(::testing::_, ::testing::_))
      .Times(::testing::Exactly(1))
      .WillRepeatedly(::testing::DoAll(
          ::testing::Invoke(mock_server_connector_, &MockServerConnector::DeleteHandler),
          ::testing::Return(kReturnOk)));
  TestUtils::FakeNowIncrement(1);
  local_registry_->RemoveExpireServiceData(Time::GetCurrentTimeMs());
  ASSERT_TRUE(mock_server_connector_->saved_handler_ == NULL);
  TestUtils::TearDownFakeTime();
}

TEST_F(InMemoryLocalRegistryTest, TestOldServiceDataGc) {
  // 创建服务
  TestUtils::SetUpFakeTime();
  EXPECT_CALL(*mock_server_connector_,
              RegisterEventHandler(::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(
          ::testing::Invoke(mock_server_connector_, &MockServerConnector::SaveHandler),
          ::testing::Return(kReturnOk)));
  ServiceData *service_data         = NULL;
  ServiceDataNotify *service_notify = NULL;
  ReturnCode ret = local_registry_->LoadServiceDataWithNotify(service_key_, kServiceDataInstances,
                                                              service_data, service_notify);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_TRUE(service_data == NULL);
  ASSERT_TRUE(service_notify != NULL);
  ASSERT_TRUE(mock_server_connector_->saved_handler_ != NULL);
  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key_);
  v1::Service *resp_service = response.mutable_service();
  resp_service->mutable_revision()->set_value("init_version");
  ServiceData *init_service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataInstances,
                                                        init_service_data);

  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(service_data, init_service_data);  // 指向同一份数据，且此时引用等于2+1
  ASSERT_EQ(service_data->DecrementAndGetRef(), 3);  // 减少一次引用后为2
  service_data->IncrementRef();  // 先加一个引用保持在使用，此时引用为2+1

  // 更新新的服务数据
  resp_service->mutable_revision()->set_value("new_version");
  ServiceData *new_service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  mock_server_connector_->saved_handler_->OnEventUpdate(service_key_, kServiceDataInstances,
                                                        new_service_data);
  service_data = NULL;
  ret = local_registry_->GetServiceDataWithRef(service_key_, kServiceDataInstances, service_data);
  ASSERT_EQ(ret, kReturnOk);
  ASSERT_EQ(service_data, new_service_data);
  // 释放获取的新服务的引用，剩余2次分别为缓存和notify中的引用
  ASSERT_EQ(service_data->DecrementAndGetRef(), 3);

  ASSERT_NE(service_data, init_service_data);  // 不等于旧服务
  TestUtils::FakeNowIncrement(2000 + 1);
  local_registry_->RunGcTask();
  // 旧服务虽然被缓存删除并被释放，但更新前在使用，所以还需要再次释放
  ASSERT_EQ(init_service_data->DecrementAndGetRef(), 1);
  delete mock_server_connector_->saved_handler_;
  TestUtils::TearDownFakeTime();
}

}  // namespace polaris
