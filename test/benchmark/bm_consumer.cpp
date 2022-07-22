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

#include <benchmark/benchmark.h>

#include <iostream>
#include <string>

#include "context/context_impl.h"
#include "mock/fake_server_response.h"
#include "polaris/consumer.h"
#include "polaris/log.h"
#include "test_utils.h"

namespace polaris {

class BM_ConsumerApi : public benchmark::Fixture {
 public:
  void SetUp(const ::benchmark::State &state) {
    if (state.thread_index != 0) {
      return;
    }
    // 设置Logger目录和日志级别
    TestUtils::CreateTempDir(log_dir_);
    // std::cout << "set log dir to " << log_dir_ << std::endl;
    polaris::SetLogDir(log_dir_);
    polaris::GetLogger()->SetLogLevel(polaris::kInfoLogLevel);

    // 创建Consumer
    TestUtils::CreateTempDir(persist_dir_);
    std::string err_msg, content =
                             "global:\n"
                             "  serverConnector:\n"
                             "    addresses: ['Fake:42']\n"
                             "consumer:\n"
                             "  localCache:\n"
                             "    persistDir: " +
                             persist_dir_;
    Config *config = Config::CreateFromString(content, err_msg);
    if (config == nullptr) {
      std::cout << "create config with error: " << err_msg << std::endl;
      exit(-1);
    }
    context_ = Context::Create(config);
    delete config;
    if (context_ == nullptr) {
      std::cout << "create context failed" << std::endl;
      exit(-1);
    }
    consumer_ = ConsumerApi::Create(context_);
    srand(Time::GetCoarseSteadyTimeMs());
  }

  void TearDown(const ::benchmark::State &state) {
    if (state.thread_index != 0) {
      return;
    }
    if (consumer_ != nullptr) {
      delete consumer_;
      consumer_ = nullptr;
    }
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
    TestUtils::RemoveDir(log_dir_);
    TestUtils::RemoveDir(persist_dir_);
  }

  std::string persist_dir_;
  std::string log_dir_;
  Context *context_;
  ConsumerApi *consumer_;
};

static ReturnCode InitServices(LocalRegistry *local_registry, benchmark::State &state) {
  ReturnCode ret_code;
  int service_num = state.range(0);
  for (int64_t i = 0; i < service_num; i++) {
    ServiceKey service_key = {"benchmark_namespace", "benchmark_service_" + std::to_string(i)};
    if ((ret_code = FakeServer::InitService(local_registry, service_key, state.range(1), false)) != kReturnOk) {
      std::string err_msg = "init services failed:" + polaris::ReturnCodeToMsg(ret_code);
      state.SkipWithError(err_msg.c_str());
      return ret_code;
    }
  }
  return kReturnOk;
}

BENCHMARK_DEFINE_F(BM_ConsumerApi, FastGetOneInstance)
(benchmark::State &state) {
  if (state.thread_index == 0) {
    InitServices(context_->GetLocalRegistry(), state);
    Location location = {"华南", "深圳", "南山"};
    context_->GetContextImpl()->GetClientLocation().Update(location);
  }
  ReturnCode ret_code;
  polaris::Instance instance;
  ServiceKey service_key = {"benchmark_namespace", "benchmark_service_0"};
  polaris::GetOneInstanceRequest request(service_key);
  polaris::ServiceCallResult result;
  while (state.KeepRunning()) {
    if ((ret_code = consumer_->GetOneInstance(request, instance)) != kReturnOk) {
      std::string err_msg = "get one instance failed:" + polaris::ReturnCodeToMsg(ret_code);
      state.SkipWithError(err_msg.c_str());
      break;
    }

    result.SetServiceNamespace(service_key.namespace_);
    result.SetServiceName(service_key.name_);
    result.SetInstanceId(instance.GetId());
    result.SetDelay(100);
    result.SetRetCode(0);
    result.SetRetStatus(polaris::kCallRetOk);
    if ((ret_code = consumer_->UpdateServiceCallResult(result)) != polaris::kReturnOk) {
      std::string err_msg = "update call result for instance with error:" + polaris::ReturnCodeToMsg(ret_code);
      state.SkipWithError(err_msg.c_str());
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_ConsumerApi, FastGetOneInstance)
    ->ArgPair(1, 1000)
    ->ThreadRange(1, 8)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->UseRealTime();

BENCHMARK_DEFINE_F(BM_ConsumerApi, SlowGetOneInstance)(benchmark::State &state) {
  if (state.thread_index == 0) {
    InitServices(context_->GetLocalRegistry(), state);
    Location location = {"华南", "深圳", "南山"};
    context_->GetContextImpl()->GetClientLocation().Update(location);
  }
  ServiceKey service_key = {"benchmark_namespace", "benchmark_service_0"};

  while (state.KeepRunning()) {
    polaris::GetOneInstanceRequest request(service_key);
    polaris::InstancesResponse *response = nullptr;
    polaris::ReturnCode ret_code;
    if ((ret_code = consumer_->GetOneInstance(request, response)) != polaris::kReturnOk) {
      std::string err_msg = "get one instance failed:" + polaris::ReturnCodeToMsg(ret_code);
      state.SkipWithError(err_msg.c_str());
    }
    std::string instance_id = response->GetInstances()[0].GetId();
    delete response;

    // 上报调用结果
    polaris::ServiceCallResult result;
    result.SetServiceNamespace(service_key.namespace_);
    result.SetServiceName(service_key.name_);
    result.SetInstanceId(instance_id);
    result.SetDelay(100);
    result.SetRetCode(0);
    result.SetRetStatus(polaris::kCallRetOk);
    if ((ret_code = consumer_->UpdateServiceCallResult(result)) != polaris::kReturnOk) {
      std::string err_msg = "update call result for instance with error:" + polaris::ReturnCodeToMsg(ret_code);
      state.SkipWithError(err_msg.c_str());
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_ConsumerApi, SlowGetOneInstance)
    ->ArgPair(1, 1000)
    ->ThreadRange(1, 8)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->UseRealTime();

BENCHMARK_DEFINE_F(BM_ConsumerApi, GetOneInstance)(benchmark::State &state) {
  if (state.thread_index == 0) {
    InitServices(context_->GetLocalRegistry(), state);
    Location location = {"华南", "深圳", "南山"};
    context_->GetContextImpl()->GetClientLocation().Update(location);
  }
  ReturnCode ret_code;
  polaris::Instance instance;
  static __thread unsigned int thread_local_seed = time(nullptr) ^ pthread_self();
  while (state.KeepRunning()) {
    ServiceKey service_key = {"benchmark_namespace",
                              "benchmark_service_" + std::to_string(rand_r(&thread_local_seed) % state.range(0))};
    polaris::GetOneInstanceRequest request(service_key);
    if ((ret_code = consumer_->GetOneInstance(request, instance)) != kReturnOk) {
      std::string err_msg = "get one instance failed:" + polaris::ReturnCodeToMsg(ret_code);
      state.SkipWithError(err_msg.c_str());
      break;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_ConsumerApi, GetOneInstance)
    ->ArgPair(1, 1000)
    ->ArgPair(10, 100)
    ->ArgPair(50, 100)
    ->ArgPair(100, 100)
    ->ArgPair(10, 500)
    ->ArgPair(50, 500)
    ->ArgPair(100, 500)
    ->ArgPair(10, 1000)
    ->ArgPair(50, 1000)
    ->ArgPair(100, 1000)
    ->ThreadRange(1, 8)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(2)
    ->UseRealTime();

}  // namespace polaris
