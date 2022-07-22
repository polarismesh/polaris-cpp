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

#include <google/protobuf/util/json_util.h>
#include <iostream>
#include <map>
#include <string>
#include <utility>

#include "logger.h"
#include "mock/fake_server_response.h"
#include "mock/mock_service_router.h"
#include "model/model_impl.h"
#include "plugin/service_router/router_chain.h"
#include "test_context.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

///////////////////////////////////////////////////////////////////////////////
// 路由数据准备测试
class RouteInfoNotifyTest : public ::testing::Test {
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    ASSERT_TRUE(context_ != nullptr);
    Location location = {"西北", "西安", "西安-长安"};
    context_->GetContextImpl()->GetClientLocation().Update(location);
    mock_local_registry_ = TestContext::SetupMockLocalRegistry(context_);
    ASSERT_TRUE(mock_local_registry_ != nullptr);
    service_key_.namespace_ = "test_namespace";
    service_key_.name_ = "test_name";
    service_router_chain_ = new ServiceRouterChain(service_key_);
    source_service_key_.namespace_ = "test_namespace";
    source_service_key_.name_ = "source_test_name";
  }

  virtual void TearDown() {
    mock_local_registry_ = nullptr;
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
    if (config_ != nullptr) {
      delete config_;
      config_ = nullptr;
    }
    if (service_router_chain_ != nullptr) {
      delete service_router_chain_;
      service_router_chain_ = nullptr;
    }
  }

 protected:
  void SetUpConfig(bool route_enable) {
    std::string content = route_enable ? "enable:\n  true" : "enable:\n  false";
    std::string err_msg;
    config_ = Config::CreateFromString(content, err_msg);
    ASSERT_TRUE(config_ != nullptr && err_msg.empty());
  }

  void CheckDataNotify(RouteInfo &route_info, ReturnCode return_code, int data_count, ServiceData *notify_data) {
    RouteInfoNotify *route_info_notify = service_router_chain_->PrepareRouteInfoWithNotify(route_info);
    timespec ts = Time::SteadyTimeAdd(0);
    if (return_code == kReturnServiceNotFound) {
      ASSERT_TRUE(route_info_notify != nullptr);
      ASSERT_FALSE(route_info_notify->IsDataReady(false));
      ASSERT_FALSE(route_info_notify->IsDataReady(true));
      ASSERT_EQ(route_info_notify->WaitData(ts), kReturnTimeout);
    } else if (return_code == kReturnNotInit) {
      ASSERT_TRUE(route_info_notify != nullptr);
      ASSERT_FALSE(route_info_notify->IsDataReady(false));
      ASSERT_TRUE(route_info_notify->IsDataReady(true));
      ASSERT_EQ(route_info_notify->WaitData(ts), kReturnOk);
    } else {
      ASSERT_TRUE(route_info_notify == nullptr);
    }
    ASSERT_EQ(mock_local_registry_->service_data_index_, data_count);
    if (return_code != kReturnOk) {
      ASSERT_EQ(mock_local_registry_->service_notify_list_.size(), 1);
      mock_local_registry_->service_notify_list_[0]->Notify(notify_data);
      ASSERT_EQ(route_info_notify->WaitData(ts), kReturnOk);
      ASSERT_TRUE(route_info_notify->IsDataReady(true));
      ASSERT_TRUE(route_info_notify->IsDataReady(false));
      ASSERT_EQ(route_info_notify->SetDataToRouteInfo(route_info), kReturnOk);
      delete route_info_notify;
    }

    ASSERT_TRUE(route_info.GetServiceInstances() != nullptr);
    if (data_count == 2) {
      ASSERT_TRUE(route_info.GetServiceRouteRule() != nullptr);
    } else if (data_count == 3) {
      ASSERT_TRUE(route_info.GetSourceServiceRouteRule() != nullptr);
    }

    // 清理数据
    mock_local_registry_->service_data_list_.clear();
    mock_local_registry_->DeleteNotify();
  }

  ServiceData *CreateDiskData(const v1::DiscoverResponse &response) {
    std::string json_content;
    google::protobuf::util::MessageToJsonString(response, &json_content);
    return ServiceData::CreateFromJson(json_content, kDataInitFromDisk, Time::GetSystemTimeMs() + 2000);
  }

 protected:
  Context *context_;
  MockLocalRegistry *mock_local_registry_;
  Config *config_;
  ServiceRouterChain *service_router_chain_;
  ServiceKey service_key_;
  ServiceKey source_service_key_;
};

// 只获取服务实例数据
TEST_F(RouteInfoNotifyTest, GetServiceInstances) {
  // 不开启路由
  SetUpConfig(false);
  ASSERT_EQ(service_router_chain_->Init(config_, context_), kReturnOk);

  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key_);
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  ServiceData *disk_service_data = CreateDiskData(response);

  std::vector<ReturnCode> return_code_list;
  return_code_list.push_back(kReturnServiceNotFound);
  return_code_list.push_back(kReturnNotInit);
  return_code_list.push_back(kReturnOk);
  for (std::size_t i = 0; i < return_code_list.size(); ++i) {
    const ReturnCode &return_code = return_code_list[i];
    std::vector<ReturnCode> expect_return;
    expect_return.push_back(return_code);
    mock_local_registry_->ExpectReturnData(expect_return);
    if (return_code == kReturnServiceNotFound) {
      mock_local_registry_->service_data_list_.push_back(nullptr);
      mock_local_registry_->ExpectReturnNotify(1);
    } else if (return_code == kReturnNotInit) {
      mock_local_registry_->service_data_list_.push_back(disk_service_data);
      mock_local_registry_->ExpectReturnNotify(1);
    } else {
      mock_local_registry_->service_data_list_.push_back(service_data);
    }

    RouteInfo route_info(service_key_, nullptr);
    CheckDataNotify(route_info, return_code, 1, service_data);
  }
  ASSERT_EQ(service_data->DecrementAndGetRef(), 0);
  ASSERT_EQ(disk_service_data->DecrementAndGetRef(), 0);
}

// 获取服务实例和服务路由
TEST_F(RouteInfoNotifyTest, GetDestServiceData) {
  // 开启路由
  SetUpConfig(true);
  ASSERT_EQ(service_router_chain_->Init(config_, context_), kReturnOk);

  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key_);
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  FakeServer::RoutingResponse(response, service_key_);
  ServiceData *service_route = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  ServiceData *disk_service_route = CreateDiskData(response);

  std::vector<ReturnCode> return_code_list;
  return_code_list.push_back(kReturnServiceNotFound);
  return_code_list.push_back(kReturnNotInit);
  return_code_list.push_back(kReturnOk);
  for (std::size_t i = 0; i < return_code_list.size(); ++i) {
    const ReturnCode &return_code = return_code_list[i];
    // 服务实例数据一直有
    std::vector<ReturnCode> expect_return;
    expect_return.push_back(kReturnOk);
    expect_return.push_back(return_code);
    mock_local_registry_->ExpectReturnData(expect_return);
    mock_local_registry_->service_data_list_.push_back(service_data);
    if (return_code == kReturnServiceNotFound) {
      mock_local_registry_->service_data_list_.push_back(nullptr);
      mock_local_registry_->ExpectReturnNotify(1);
    } else if (return_code == kReturnNotInit) {
      mock_local_registry_->service_data_list_.push_back(disk_service_route);
      mock_local_registry_->ExpectReturnNotify(1);
    } else {
      mock_local_registry_->service_data_list_.push_back(service_route);
    }

    RouteInfo route_info(service_key_, nullptr);
    CheckDataNotify(route_info, return_code, 2, service_route);
  }
  ASSERT_EQ(service_data->DecrementAndGetRef(), 0);
  ASSERT_EQ(service_route->DecrementAndGetRef(), 0);
  ASSERT_EQ(disk_service_route->DecrementAndGetRef(), 0);
}

// 获取服务实例和服务路由、源服务路由
TEST_F(RouteInfoNotifyTest, GetAllServiceData) {
  // 开启路由
  SetUpConfig(true);
  ASSERT_EQ(service_router_chain_->Init(config_, context_), kReturnOk);

  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key_);
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  FakeServer::RoutingResponse(response, service_key_);
  ServiceData *service_route = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  FakeServer::RoutingResponse(response, source_service_key_);
  ServiceData *source_service_route = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  ServiceData *disk_source_service_route = CreateDiskData(response);

  std::vector<ReturnCode> return_code_list;
  return_code_list.push_back(kReturnServiceNotFound);
  return_code_list.push_back(kReturnNotInit);
  return_code_list.push_back(kReturnOk);
  for (std::size_t i = 0; i < return_code_list.size(); ++i) {
    const ReturnCode &return_code = return_code_list[i];
    // 服务实例和路由数据
    std::vector<ReturnCode> expect_return;
    expect_return.push_back(kReturnOk);
    expect_return.push_back(kReturnOk);
    expect_return.push_back(return_code);
    mock_local_registry_->ExpectReturnData(expect_return);
    mock_local_registry_->service_data_list_.push_back(service_data);
    mock_local_registry_->service_data_list_.push_back(service_route);
    if (return_code == kReturnServiceNotFound) {
      mock_local_registry_->service_data_list_.push_back(nullptr);
      mock_local_registry_->ExpectReturnNotify(1);
    } else if (return_code == kReturnNotInit) {
      mock_local_registry_->service_data_list_.push_back(disk_source_service_route);
      mock_local_registry_->ExpectReturnNotify(1);
    } else {
      mock_local_registry_->service_data_list_.push_back(source_service_route);
    }

    ServiceInfo source_service_info;
    source_service_info.service_key_ = source_service_key_;
    RouteInfo route_info(service_key_, &source_service_info);
    CheckDataNotify(route_info, return_code, 3, source_service_route);
  }
  ASSERT_EQ(service_data->DecrementAndGetRef(), 0);
  ASSERT_EQ(service_route->DecrementAndGetRef(), 0);
  ASSERT_EQ(source_service_route->DecrementAndGetRef(), 0);
  ASSERT_EQ(disk_source_service_route->DecrementAndGetRef(), 0);
}

TEST_F(RouteInfoNotifyTest, PrepareData) {
  // 不开启路由
  SetUpConfig(false);
  ASSERT_EQ(service_router_chain_->Init(config_, context_), kReturnOk);

  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key_);
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  ServiceData *disk_service_data = CreateDiskData(response);

  std::vector<ReturnCode> return_code_list;
  return_code_list.push_back(kReturnServiceNotFound);
  return_code_list.push_back(kReturnNotInit);
  for (std::size_t i = 0; i < return_code_list.size(); ++i) {
    const ReturnCode &return_code = return_code_list[i];
    std::vector<ReturnCode> expect_return;
    expect_return.push_back(return_code);
    mock_local_registry_->ExpectReturnData(expect_return);
    if (return_code == kReturnServiceNotFound) {
      mock_local_registry_->service_data_list_.push_back(nullptr);
    } else {
      mock_local_registry_->service_data_list_.push_back(disk_service_data);
    }
    mock_local_registry_->ExpectReturnNotify(1);

    RouteInfo route_info(service_key_, nullptr);
    if (return_code == kReturnServiceNotFound) {
      ASSERT_EQ(service_router_chain_->PrepareRouteInfo(route_info, 0), kReturnTimeout);
      std::vector<ReturnCode> expect_return;
      expect_return.push_back(kReturnOk);
      mock_local_registry_->ExpectReturnData(expect_return);
      mock_local_registry_->service_data_list_.push_back(service_data);
      ASSERT_EQ(service_router_chain_->PrepareRouteInfo(route_info, 0), kReturnOk);
    } else {
      ASSERT_EQ(service_router_chain_->PrepareRouteInfo(route_info, 0), kReturnOk);
    }
    mock_local_registry_->DeleteNotify();
  }
  ASSERT_EQ(service_data->DecrementAndGetRef(), 0);
  ASSERT_EQ(disk_service_data->DecrementAndGetRef(), 0);
}

///////////////////////////////////////////////////////////////////////////////
// 路由链执行测试
class ServiceRouterChainTest : public ::testing::Test {
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    ASSERT_TRUE(context_ != nullptr);
    std::string content =
        "chain:\n"
        "  - mockRouter\n"
        "  - mockRouter";
    std::string err_msg;
    config_ = Config::CreateFromString(content, err_msg);
    MockServiceRouter::RegisterMockPlugin();
    MockServiceRouterInit();
    service_key_.namespace_ = "test_namespace";
    service_key_.name_ = "test_name";
    service_router_chain_ = new ServiceRouterChain(service_key_);
  }

  virtual void TearDown() {
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
    if (config_ != nullptr) {
      delete config_;
      config_ = nullptr;
    }
    if (service_router_chain_ != nullptr) {
      delete service_router_chain_;
      service_router_chain_ = nullptr;
    }
    MockServiceRouter::mock_service_router_list_.clear();
    if (service_data_ != nullptr) {
      EXPECT_EQ(service_data_->DecrementAndGetRef(), 0);
      service_data_ = nullptr;
    }
  }

 protected:
  Context *context_;
  Config *config_;
  ServiceRouterChain *service_router_chain_;
  ServiceKey service_key_;
  ServiceData *service_data_;
};

TEST_F(ServiceRouterChainTest, DoRoute) {
  ASSERT_EQ(MockServiceRouter::mock_service_router_list_.size(), 2);
  MockServiceRouter *first_service_router = MockServiceRouter::mock_service_router_list_[0];
  MockServiceRouter *second_service_router = MockServiceRouter::mock_service_router_list_[1];
  EXPECT_CALL(*first_service_router, Init(::testing::_, ::testing::_)).WillOnce(::testing::Return(kReturnOk));
  EXPECT_CALL(*second_service_router, Init(::testing::_, ::testing::_)).WillOnce(::testing::Return(kReturnOk));
  ASSERT_EQ(service_router_chain_->Init(config_, context_), kReturnOk);

  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key_);
  for (int i = 0; i < 3; i++) {
    v1::Instance *instance = response.add_instances();
    instance->mutable_id()->set_value("instance_" + std::to_string(i));
    instance->mutable_host()->set_value("host");
    instance->mutable_port()->set_value(i);
    instance->mutable_weight()->set_value(100);
  }
  service_data_ = ServiceData::CreateFromPb(&response, kDataIsSyncing);

  EXPECT_CALL(*first_service_router, DoRoute(::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::Invoke(first_service_router, &MockServiceRouter::DropFirstInstance),
                                 ::testing::Return(kReturnOk)));
  EXPECT_CALL(*second_service_router, DoRoute(::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::Invoke(second_service_router, &MockServiceRouter::DropFirstInstance),
                                 ::testing::Return(kReturnOk)));
  RouteInfo route_info(service_key_, nullptr);
  route_info.SetServiceInstances(new ServiceInstances(service_data_));
  RouteResult route_result;
  ASSERT_EQ(service_router_chain_->DoRoute(route_info, &route_result), kReturnOk);
  ServiceInstances *route_instances = route_info.GetServiceInstances();
  ASSERT_TRUE(route_instances != nullptr);
  InstancesSet *instances_set = route_instances->GetAvailableInstances();
  ASSERT_EQ(instances_set->GetInstances().size(), 1);
  route_info.SetServiceInstances(nullptr);
  delete route_instances;

  EXPECT_CALL(*first_service_router, DoRoute(::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::Invoke(first_service_router, &MockServiceRouter::DropFirstInstance),
                                 ::testing::Return(kReturnOk)));
  EXPECT_CALL(*second_service_router, DoRoute(::testing::_, ::testing::_))
      .WillOnce(::testing::Return(kReturnServiceNotFound));
  route_info.SetServiceInstances(new ServiceInstances(service_data_));
  ASSERT_EQ(service_router_chain_->DoRoute(route_info, &route_result), kReturnServiceNotFound);
}

TEST_F(ServiceRouterChainTest, RouteRuleNotMatch) {
  EXPECT_CALL(*MockServiceRouter::mock_service_router_list_[0], Init(::testing::_, ::testing::_))
      .WillOnce(::testing::Return(kReturnOk));
  EXPECT_CALL(*MockServiceRouter::mock_service_router_list_[1], Init(::testing::_, ::testing::_))
      .WillOnce(::testing::Return(kReturnOk));
  ASSERT_EQ(service_router_chain_->Init(config_, context_), kReturnOk);

  EXPECT_CALL(*MockServiceRouter::mock_service_router_list_[0], DoRoute(::testing::_, ::testing::_))
      .WillRepeatedly(::testing::Return(kReturnRouteRuleNotMatch));

  v1::DiscoverResponse response;
  FakeServer::CreateServiceInstances(response, service_key_, 10);
  service_data_ = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  FakeServer::CreateServiceRoute(response, service_key_, false);
  ServiceData *service_route_ = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  for (int i = 0; i < 2; i++) {
    ServiceInfo source_service_info;
    RouteInfo route_info(service_key_, &source_service_info);
    route_info.SetServiceInstances(new ServiceInstances(service_data_));
    route_info.SetServiceRouteRule(new ServiceRouteRule(service_route_));
    if (i > 0) {
      route_info.SetSourceServiceRouteRule(new ServiceRouteRule(service_route_));
    }
    RouteResult route_result;
    ASSERT_EQ(service_router_chain_->DoRoute(route_info, &route_result), kReturnRouteRuleNotMatch);
  }
  EXPECT_EQ(service_route_->DecrementAndGetRef(), 0);
}

}  // namespace polaris
