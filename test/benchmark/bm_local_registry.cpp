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

#include "polaris/context.h"
#include "polaris/log.h"
#include "test_utils.h"
#include "v1/response.pb.h"

namespace polaris {

class BM_LocalRegistry : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State &state) {
    if (state.thread_index != 0) {
      return;
    }
    // 设置Logger目录和日志级别
    TestUtils::CreateTempDir(log_dir_);
    // std::cout << "set log dir to " << log_dir_ << std::endl;
    SetLogDir(log_dir_);
    GetLogger()->SetLogLevel(kInfoLogLevel);

    // 创建Context
    TestUtils::CreateTempDir(persist_dir_);
    std::string err_msg, content =
                             "global:\n"
                             "  serverConnector:\n"
                             "    addresses: ['Fake:42']"
                             "\nconsumer:\n"
                             "  localCache:\n"
                             "    persistDir: " +
                             persist_dir_;
    Config *config = Config::CreateFromString(content, err_msg);
    if (config == NULL) {
      std::cout << "create config with error: " << err_msg << std::endl;
      exit(-1);
    }
    context_ = Context::Create(config);
    delete config;
    if (context_ == NULL) {
      std::cout << "create context failed" << std::endl;
      exit(-1);
    }
    local_registry_ = context_->GetLocalRegistry();
    srand(Time::GetCurrentTimeMs());
  }

  void TearDown(const ::benchmark::State &state) {
    if (state.thread_index != 0) {
      return;
    }
    local_registry_ = NULL;
    if (context_ != NULL) {
      delete context_;
      context_ = NULL;
    }
    TestUtils::RemoveDir(log_dir_);
    TestUtils::RemoveDir(persist_dir_);
  }

  std::string persist_dir_;
  std::string log_dir_;
  Context *context_;
  LocalRegistry *local_registry_;
};

static ServiceData *CreateService(const ServiceKey &service_key) {
  v1::DiscoverResponse response;
  response.set_type(v1::DiscoverResponse::INSTANCE);
  v1::Service *service = response.mutable_service();
  service->mutable_namespace_()->set_value(service_key.namespace_);
  service->mutable_name()->set_value(service_key.name_);
  ::v1::Instance *instance = response.mutable_instances()->Add();
  instance->mutable_id()->set_value("instance_id");
  instance->mutable_namespace_()->set_value(service_key.namespace_);
  instance->mutable_service()->set_value(service_key.name_);
  instance->mutable_host()->set_value("host");
  instance->mutable_port()->set_value(9000);
  return ServiceData::CreateFromPb(&response, kDataIsSyncing);
}

static ReturnCode InitServices(LocalRegistry *local_registry, int64_t service_num) {
  ReturnCode ret_code;
  for (int64_t i = 0; i < service_num; i++) {
    ServiceKey service_key         = {"benchmark_namespace",
                              "benchmark_service_" + StringUtils::TypeToStr<int>(i)};
    ServiceDataNotify *data_notify = NULL;
    ServiceData *service_data      = NULL;
    ret_code = local_registry->LoadServiceDataWithNotify(service_key, kServiceDataInstances,
                                                         service_data, data_notify);
    if (ret_code != kReturnOk) {
      return ret_code;
    }
    service_data = CreateService(service_key);
    ret_code = local_registry->UpdateServiceData(service_key, kServiceDataInstances, service_data);
    if (ret_code != kReturnOk) {
      return ret_code;
    }
  }
  return kReturnOk;
}

BENCHMARK_DEFINE_F(BM_LocalRegistry, GetServiceData)(benchmark::State &state) {
  if (state.thread_index == 0) {
    InitServices(local_registry_, state.range(0));
  }
  static __thread unsigned int thread_local_seed = time(NULL) ^ pthread_self();
  while (state.KeepRunning()) {
    ServiceKey service_key = {
        "benchmark_namespace",
        "benchmark_service_" +
            StringUtils::TypeToStr<int>(rand_r(&thread_local_seed) % state.range(0))};
    ServiceData *service_data = NULL;
    ReturnCode ret_code =
        local_registry_->GetServiceDataWithRef(service_key, kServiceDataInstances, service_data);
    if (ret_code != kReturnOk || service_data == NULL) {
      state.SkipWithError("get service data return error");
      break;
    }
    service_data->DecrementRef();
    service_data = NULL;
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_LocalRegistry, GetServiceData)
    ->ThreadRange(1, 8)
    ->RangeMultiplier(10)
    ->Range(1, 10)
    ->UseRealTime();

}  // namespace polaris
