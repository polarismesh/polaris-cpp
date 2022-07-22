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
#include <iomanip>
#include <iostream>

#include "context/context_impl.h"
#include "mock/fake_server_response.h"
#include "mock/mock_server_connector.h"
#include "polaris/consumer.h"
#include "polaris/plugin.h"
#include "test_utils.h"
#include "utils/file_utils.h"
#include "utils/time_clock.h"

#include "plugin/load_balancer/locality_aware/locality_aware.h"

namespace polaris {

class LocalityAwareLBBaseTest : public ::testing::Test {
 protected:
  LocalityAwareLBBaseTest() { srand(time(nullptr)); }
  virtual ~LocalityAwareLBBaseTest() {}
  virtual void SetUp() {}
  virtual void TearDown() {}
};

///////////////////////////////////////////////////////////////////////////////
//测试doubly_buffered_data读写数据

struct Foo {
  Foo() : x(0) {}
  uint64_t x;
};

bool AddN(Foo &f, int n) {
  f.x += n;
  return true;
}

TEST_F(LocalityAwareLBBaseTest, TestDoublyBufferedData) {
  DoublyBufferedData<Foo> d;
  uint64_t sum = 0;
  for (uint64_t i = 1; i < 10000; ++i) {
    d.Modify(AddN, i);  // 改写数据
    sum += i;
    {
      DoublyBufferedData<Foo>::ScopedPtr ptr;
      ASSERT_EQ(0, d.Read(&ptr));  // 读取数据
      ASSERT_EQ(sum, ptr->x);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
//测试circular_queue读写数据

TEST_F(LocalityAwareLBBaseTest, TestCircularQueue) {
  size_t queue_size = 128;
  CircularQueue<Foo> q(queue_size);
  Foo data;

  for (size_t round = 0; round < 10; ++round) {
    // 创建时，对列为空，size为0
    ASSERT_TRUE(q.Empty());
    ASSERT_EQ(0, q.Size());

    // push数据
    for (size_t i = 0; i < queue_size; ++i) {
      data.x = static_cast<uint64_t>(i);
      ASSERT_TRUE(q.Push(data));
    }

    // 核对容器是否满了
    ASSERT_TRUE(q.Full());
    ASSERT_EQ(128, q.Size());

    // 容器满时push数据为false
    for (size_t i = 0; i < queue_size; ++i) {
      ASSERT_FALSE(q.Push(data));
    }

    // 取top数据进行核对，并讲其弹出
    for (size_t i = 0; i < queue_size; ++i) {
      Foo *data = q.Top();
      ASSERT_TRUE(data != nullptr);
      ASSERT_EQ(data->x, i);
      ASSERT_TRUE(q.Pop());
    }

    // 容器为空
    ASSERT_TRUE(q.Empty());
    ASSERT_EQ(0, q.Size());

    // 取top返回NULL，pop返回false
    for (size_t i = 0; i < queue_size; ++i) {
      Foo *data = q.Top();
      ASSERT_TRUE(data == nullptr);
      ASSERT_FALSE(q.Pop());
    }

    // 测试 elim_push
    for (size_t i = 0; i < queue_size; ++i) {
      data.x = static_cast<uint64_t>(i);
      q.ElimPush(data);
    }

    ASSERT_EQ(q.Top()->x, 0);
    ASSERT_EQ(q.Bottom()->x, queue_size - 1);
    ASSERT_TRUE(q.Full());

    // 继续用elim_push写入
    for (size_t i = 0; i < queue_size; ++i) {
      data.x = static_cast<uint64_t>(queue_size - i);
      q.ElimPush(data);
    }

    // 核对elim_push写的数据
    for (size_t i = 0; i < queue_size; ++i) {
      Foo *data = q.Top();
      ASSERT_TRUE(data != nullptr);
      ASSERT_EQ(data->x, queue_size - i);
      ASSERT_TRUE(q.Pop());
    }

    // 清空数据后，可以正常使用
    q.Clear();

    for (size_t i = 0; i < queue_size; ++i) {
      data.x = static_cast<uint64_t>(i);
      ASSERT_TRUE(q.Push(data));
    }

    ASSERT_TRUE(q.Full());
    q.Clear();
    ASSERT_TRUE(q.Empty());
    ASSERT_TRUE(q.Top() == nullptr);

    for (size_t i = 0; i < queue_size; ++i) {
      data.x = static_cast<uint64_t>(i);
      ASSERT_TRUE(q.Push(data));
    }

    ASSERT_TRUE(q.Full());

    for (size_t i = 0; i < queue_size; ++i) {
      Foo *data = q.Top();
      ASSERT_TRUE(data != nullptr);
      ASSERT_EQ(data->x, i);
      ASSERT_TRUE(q.Pop());
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
//测试LocalityAwareSelector

TEST_F(LocalityAwareLBBaseTest, TestLocalityAwareSelector) {
  LocalityAwareSelector lalb(1000);
  SelectIn in;
  SelectOut out;
  in.begin_time_us = 0;
  in.changable_weights = false;

  // 无节点时返回kReturnInstanceNotFound
  ASSERT_EQ(kReturnInstanceNotFound, lalb.SelectInstance(in, &out));

  // 向selector中注册实例
  std::vector<InstanceId> ids;
  const int instance_num = 100;
  for (int i = 0; i < instance_num; ++i) {
    char addr[32];
    snprintf(addr, sizeof(addr), "instance:%d", i);
    std::string instance_id = addr;
    ids.push_back(instance_id);
    ASSERT_TRUE(lalb.AddInstance(instance_id));
  }

  // 尝试添加存在的节点，返回false
  for (int i = 0; i < instance_num; ++i) {
    char addr[32];
    snprintf(addr, sizeof(addr), "instance:%d", i);
    std::string instance_id = addr;
    ids.push_back(instance_id);
    ASSERT_FALSE(lalb.AddInstance(instance_id));
  }

  // 尝试删除不存在的节点，返回false
  for (int i = instance_num + 10; i < instance_num + 100; ++i) {
    char addr[32];
    snprintf(addr, sizeof(addr), "instance:%d", i);
    std::string instance_id = addr;
    ids.push_back(instance_id);
    ASSERT_FALSE(lalb.RemoveInstance(instance_id));
  }

  // 删除尾部节点的方法和其他节点的方法是不同的，测试两种删除
  // 删除位于weight_tree尾部节点
  for (size_t i = 0; i < instance_num / 4; ++i) {
    ASSERT_TRUE(lalb.RemoveInstance(ids[instance_num - i - 1]));  // 删除selector中注册实例
  }

  // 删除位于weight_tree首部节点
  for (size_t i = 0; i < instance_num / 4; ++i) {
    ASSERT_TRUE(lalb.RemoveInstance(ids[i]));
  }

  // 添加新节点
  for (size_t i = 0; i < instance_num / 4; ++i) {
    char addr[32];
    snprintf(addr, sizeof(addr), "instance:%d", static_cast<int>(i + instance_num + 100));
    std::string instance_id = addr;
    ASSERT_TRUE(lalb.AddInstance(instance_id));
  }
}

///////////////////////////////////////////////////////////////////////////////
// 测试LocalityAwareLoadBalancer

volatile bool global_stop = false;

struct CountInfo {
  uint64_t total_count;
  uint64_t correct_count;
};

struct SelectInfo {
  ConsumerApi *consumer_api;
  ServiceKey service_key;
};

class LocalityAwareLBTest : public MockServerConnectorTest {
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
                             persist_dir_;
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
    instance_num_ = 10;
    instance_healthy_ = true;

    v1::CircuitBreaker *cb = circuit_breaker_pb_response_.mutable_circuitbreaker();
    cb->mutable_name()->set_value("xxx");
    cb->mutable_namespace_()->set_value("xxx");
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
    for (int i = 0; i < instance_num_ + 2; i++) {
      ::v1::Instance *instance = instances_response_.mutable_instances()->Add();
      instance->mutable_namespace_()->set_value(service_key_.namespace_);
      instance->mutable_service()->set_value(service_key_.name_);
      instance->mutable_id()->set_value("instance_" + std::to_string(i));
      instance->mutable_host()->set_value("host" + std::to_string(i));
      instance->mutable_port()->set_value(8080 + i);
      instance->mutable_healthy()->set_value(instance_healthy_);
      instance->mutable_weight()->set_value(i != instance_num_ ? 100 : 0);  // 第11个权重为0
      if (i == instance_num_ + 1) {
        instance->mutable_isolate()->set_value(true);  // 第12个隔离
      }
    }
    FakeServer::RoutingResponse(routing_response_, service_key_);
  }

 public:
  void MockFireEventHandler(const ServiceKey &service_key, ServiceDataType data_type, uint64_t /*sync_interval*/,
                            const std::string & /*disk_revision*/, ServiceEventHandler *handler) {
    ServiceData *service_data;
    if (data_type == kServiceDataInstances) {
      service_data = ServiceData::CreateFromPb(&instances_response_, kDataIsSyncing);
    } else if (data_type == kServiceDataRouteRule) {
      service_data = ServiceData::CreateFromPb(&routing_response_, kDataIsSyncing);
    } else if (data_type == kCircuitBreakerConfig) {
      service_data = ServiceData::CreateFromPb(&circuit_breaker_pb_response_, kDataIsSyncing);
    } else {
      service_data = ServiceData::CreateFromPb(&routing_response_, kDataIsSyncing);
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
  v1::DiscoverResponse circuit_breaker_pb_response_;
  ServiceKey service_key_;
  int instance_num_;
  bool instance_healthy_;
  std::string persist_dir_;
  std::vector<pthread_t> event_thread_list_;
};

void *SelectWithUpdate(void *arg) {
  CountInfo *count_info = new CountInfo;
  SelectInfo *select_info = reinterpret_cast<SelectInfo *>(arg);
  ReturnCode ret;
  Instance instance;
  ServiceCallResult result;
  GetOneInstanceRequest request(select_info->service_key);
  request.SetLoadBalanceType(polaris::kLoadBalanceTypeLocalityAware);
  result.SetServiceNamespace(select_info->service_key.namespace_);
  result.SetServiceName(select_info->service_key.name_);
  result.SetDelay(20000);

  while (!global_stop) {
    ++(count_info->total_count);
    ret = select_info->consumer_api->GetOneInstance(request, instance);
    if (ret == kReturnOk) {
      ++(count_info->correct_count);
    } else {
      continue;
    }
    // 上报调用结果
    result.SetInstanceId(instance.GetId());
    result.SetLocalityAwareInfo(instance.GetLocalityAwareInfo());
    if (ret == polaris::kReturnOk) {
      result.SetRetCode(0);
      result.SetRetStatus(polaris::kCallRetOk);
    } else {
      result.SetRetCode(ret);
      result.SetRetStatus(ret == polaris::kReturnTimeout ? polaris::kCallRetTimeout : polaris::kCallRetError);
    }
    ret = select_info->consumer_api->UpdateServiceCallResult(result);
  }
  return count_info;
}

TEST_F(LocalityAwareLBTest, TestSelectWithUpdate) {
  instance_num_ = 200;
  InitServiceData();
  EXPECT_CALL(*server_connector_,
              RegisterEventHandler(::testing::Eq(service_key_), ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::Exactly(2))
      .WillRepeatedly(::testing::DoAll(::testing::Invoke(this, &LocalityAwareLBTest::MockFireEventHandler),
                                       ::testing::Return(kReturnOk)));

  // 创建子线程
  global_stop = false;
  SelectInfo *select_info = new SelectInfo;
  select_info->consumer_api = consumer_api_;
  select_info->service_key = service_key_;
  size_t pthread_num = 3;
  pthread_t th[pthread_num];
  for (size_t i = 0; i < pthread_num; ++i) {
    ASSERT_EQ(0, pthread_create(&th[i], nullptr, SelectWithUpdate, reinterpret_cast<void *>(select_info)));
  }

  // 在本线程进行Select和Update
  uint64_t total_count = 10000;
  ReturnCode ret;
  Instance instance;
  ServiceCallResult result;
  GetOneInstanceRequest request(service_key_);
  request.SetLoadBalanceType(polaris::kLoadBalanceTypeLocalityAware);
  result.SetServiceNamespace(service_key_.namespace_);
  result.SetServiceName(service_key_.name_);
  result.SetDelay(20000);

  for (uint64_t i = 0; i < total_count; i++) {
    ret = consumer_api_->GetOneInstance(request, instance);
    ASSERT_EQ(ret, kReturnOk);
    // 上报调用结果
    result.SetInstanceId(instance.GetId());
    result.SetLocalityAwareInfo(instance.GetLocalityAwareInfo());

    if (ret == polaris::kReturnOk) {
      result.SetRetCode(0);
      result.SetRetStatus(polaris::kCallRetOk);
    } else {
      result.SetRetCode(ret);
      result.SetRetStatus(ret == polaris::kReturnTimeout ? polaris::kCallRetTimeout : polaris::kCallRetError);
    }
    ret = consumer_api_->UpdateServiceCallResult(result);
    ASSERT_EQ(ret, kReturnOk);
  }
  global_stop = true;
  void *retval[pthread_num];
  for (size_t i = 0; i < pthread_num; ++i) {
    ASSERT_EQ(0, pthread_join(th[i], &retval[i]));
  }
  for (size_t i = 0; i < pthread_num; ++i) {
    CountInfo *count_info = reinterpret_cast<CountInfo *>(retval[i]);
    delete count_info;
  }
  delete select_info;
}
}  // namespace polaris