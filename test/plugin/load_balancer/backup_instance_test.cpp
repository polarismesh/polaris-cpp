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

#include "context/context_impl.h"
#include "mock/fake_server_response.h"
#include "mock/mock_server_connector.h"
#include "polaris/consumer.h"
#include "polaris/plugin.h"
#include "test_utils.h"
#include "utils/file_utils.h"
#include "utils/time_clock.h"

namespace polaris {

class BackupInstanceMockServerConnectorTest : public MockServerConnectorTest {
 protected:
  virtual void SetUp() {
    MockServerConnectorTest::SetUp();
    context_ = nullptr;
    consumer_api_ = nullptr;
    TestUtils::CreateTempDir(persist_dir_);
    std::string err_msg, content =
                             "global:\n"
                             "  serverConnector:\n"
                             "    protocol: " +
                             server_connector_plugin_name_ +
                             "\nconsumer:\n"
                             "  localCache:\n"
                             "    persistDir: " +
                             persist_dir_ + "\n" +
                             "  loadBalancer:\n"
                             "    type: l5cst\n";
    Config *config = Config::CreateFromString(content, err_msg);
    POLARIS_ASSERT(config != nullptr && err_msg.empty());
    context_ = Context::Create(config);
    delete config;
    ASSERT_TRUE(context_ != nullptr);
    ASSERT_TRUE((consumer_api_ = ConsumerApi::Create(context_)) != nullptr);

    // check
    MockServerConnector *server_connector_in_context =
        dynamic_cast<MockServerConnector *>(context_->GetContextImpl()->GetServerConnector());
    ASSERT_TRUE(server_connector_ != nullptr);
    ASSERT_EQ(server_connector_, server_connector_in_context);
    service_key_.name_ = "cpp_test_service";
    service_key_.namespace_ = "cpp_test_namespace";
    instance_num_ = 100;
    instance_healthy_ = true;
  }

  virtual void TearDown() {
    if (consumer_api_ != nullptr) {
      delete consumer_api_;
      consumer_api_ = nullptr;
    }
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
    TestUtils::RemoveDir(persist_dir_);
    for (std::size_t i = 0; i < event_thread_list_.size(); ++i) {
      pthread_join(event_thread_list_[i], nullptr);
    }
    MockServerConnectorTest::TearDown();
  }

  void InitServiceData() {
    FakeServer::InstancesResponse(instances_response_, service_key_);
    v1::Service *service = instances_response_.mutable_service();
    for (int i = 0; i < 10; i++) {
      (*service->mutable_metadata())["key" + std::to_string(i)] = "value" + std::to_string(i);
    }
    for (int i = 0; i < instance_num_; i++) {
      ::v1::Instance *instance = instances_response_.mutable_instances()->Add();
      instance->mutable_namespace_()->set_value(service_key_.namespace_);
      instance->mutable_service()->set_value(service_key_.name_);
      instance->mutable_id()->set_value("instance_" + std::to_string(i));
      instance->mutable_host()->set_value("host" + std::to_string(i));
      instance->mutable_port()->set_value(8080 + i);
      instance->mutable_healthy()->set_value(instance_healthy_);
      instance->mutable_weight()->set_value(100);
    }
    FakeServer::RoutingResponse(routing_response_, service_key_);
  }

 public:
  void MockFireEventHandler(const ServiceKey &service_key, ServiceDataType data_type, uint64_t /*sync_interval*/,
                            const std::string & /*disk_revision*/, ServiceEventHandler *handler) {
    ServiceData *service_data = nullptr;
    if (data_type == kServiceDataInstances) {
      service_data = ServiceData::CreateFromPb(&instances_response_, kDataIsSyncing);
    } else if (data_type == kServiceDataRouteRule) {
      service_data = ServiceData::CreateFromPb(&routing_response_, kDataIsSyncing);
    } else {
      EXPECT_TRUE(false) << "unexpected data type:" << data_type;
    }
    // 创建单独的线程去下发数据更新，否则会死锁
    EventHandlerData *event_data = new EventHandlerData();
    event_data->service_key_ = service_key;
    event_data->data_type_ = data_type;
    event_data->service_data_ = service_data;
    event_data->handler_ = handler;
    pthread_t tid;
    pthread_create(&tid, nullptr, AsyncEventUpdate, event_data);
    handler_list_.push_back(handler);
    event_thread_list_.push_back(tid);
  }

 protected:
  Context *context_;
  ConsumerApi *consumer_api_;
  v1::DiscoverResponse instances_response_;
  v1::DiscoverResponse routing_response_;
  ServiceKey service_key_;
  int instance_num_;
  bool instance_healthy_;
  std::string persist_dir_;
  std::vector<pthread_t> event_thread_list_;
};

bool CompareInstance(Instance instance1, Instance instance2) { return instance1.GetId() < instance2.GetId(); }

bool CheckDuplicate(std::vector<Instance> &instances) {
  sort(instances.begin(), instances.end(), CompareInstance);
  for (size_t i = 1; i < instances.size(); ++i) {
    if (instances[i].GetId() == instances[i - 1].GetId()) {
      return false;
    }
  }
  return true;
}

TEST_F(BackupInstanceMockServerConnectorTest, TestSetAndGetRoute) {
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(
          ::testing::DoAll(::testing::Invoke(this, &BackupInstanceMockServerConnectorTest::MockFireEventHandler),
                           ::testing::Return(kReturnOk)));

  ReturnCode ret;
  Instance instance;
  GetOneInstanceRequest request(service_key_);

  // 不设置backup_instance_num，返回一个实例
  request.SetLoadBalanceType(kLoadBalanceTypeWeightedRandom);
  for (uint32_t i = 0; i < 1000; i++) {
    InstancesResponse *resp;
    ret = consumer_api_->GetOneInstance(request, resp);
    ASSERT_EQ(ret, kReturnOk);
    std::vector<Instance> instances = resp->GetInstances();
    ASSERT_EQ(instances.size(), 1);
    ASSERT_TRUE(CheckDuplicate(instances));
    delete resp;
  }

  // 正常返回个数
  request.SetLoadBalanceType(kLoadBalanceTypeWeightedRandom);
  for (uint32_t i = 0; i < 20UL; i++) {
    InstancesResponse *resp;
    request.SetBackupInstanceNum(i);
    ret = consumer_api_->GetOneInstance(request, resp);
    ASSERT_EQ(ret, kReturnOk);
    std::vector<Instance> instances = resp->GetInstances();
    uint32_t ans_num = (i + 1) > static_cast<uint32_t>(instance_num_) ? instance_num_ : (i + 1);
    ASSERT_EQ(instances.size(), ans_num);
    ASSERT_TRUE(CheckDuplicate(instances));
    delete resp;
  }

  // kLoadBalanceTypeRingHash
  request.SetLoadBalanceType(kLoadBalanceTypeRingHash);
  for (uint32_t i = 0; i < 20UL; i++) {
    InstancesResponse *resp;
    request.SetBackupInstanceNum(i);
    request.SetHashKey(100);
    ret = consumer_api_->GetOneInstance(request, resp);
    ASSERT_EQ(ret, kReturnOk);
    std::vector<Instance> instances = resp->GetInstances();
    uint32_t ans_num = (i + 1) > static_cast<uint32_t>(instance_num_) ? instance_num_ : (i + 1);
    ASSERT_EQ(instances.size(), ans_num);
    ASSERT_TRUE(CheckDuplicate(instances));
    delete resp;
  }

  // kLoadBalanceTypeL5CstHash
  request.SetLoadBalanceType(kLoadBalanceTypeL5CstHash);
  for (uint32_t i = 0; i < 20UL; i++) {
    InstancesResponse *resp;
    request.SetBackupInstanceNum(i);
    request.SetHashKey(100);
    ret = consumer_api_->GetOneInstance(request, resp);
    ASSERT_EQ(ret, kReturnOk);
    std::vector<Instance> instances = resp->GetInstances();
    uint32_t ans_num = (i + 1) > static_cast<uint32_t>(instance_num_) ? instance_num_ : (i + 1);
    ASSERT_EQ(instances.size(), ans_num);
    ASSERT_TRUE(CheckDuplicate(instances));
    delete resp;
  }

  // kLoadBalanceTypeCMurmurHash
  request.SetLoadBalanceType(kLoadBalanceTypeCMurmurHash);
  for (uint32_t i = 0; i < 20UL; i++) {
    InstancesResponse *resp;
    request.SetBackupInstanceNum(i);
    request.SetHashKey(100);
    ret = consumer_api_->GetOneInstance(request, resp);
    ASSERT_EQ(ret, kReturnOk);
    std::vector<Instance> instances = resp->GetInstances();
    uint32_t ans_num = (i + 1) > static_cast<uint32_t>(instance_num_) ? instance_num_ : (i + 1);
    ASSERT_EQ(instances.size(), ans_num);
    ASSERT_TRUE(CheckDuplicate(instances));
    delete resp;
  }
}

}  // namespace polaris