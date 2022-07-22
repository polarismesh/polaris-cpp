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

#ifndef POLARIS_CPP_TEST_BENCHMARK_CONTEXT_FIXTURE_H_
#define POLARIS_CPP_TEST_BENCHMARK_CONTEXT_FIXTURE_H_

#include <benchmark/benchmark.h>

#include <iostream>
#include <string>

#include "context/context_impl.h"
#include "mock/fake_server_response.h"
#include "polaris/context.h"
#include "polaris/log.h"
#include "test_utils.h"

namespace polaris {

class ContextFixture : public ::benchmark::Fixture {
 public:
  ContextFixture() {  // 设置Logger目录和日志级别
    TestUtils::CreateTempDir(log_dir_);
    polaris::SetLogDir(log_dir_);
    polaris::GetLogger()->SetLogLevel(polaris::kInfoLogLevel);

    // 创建持久化目录
    TestUtils::CreateTempDir(persist_dir_);
    std::cout << "log dir:" << log_dir_ << ", persist dir:" << persist_dir_ << std::endl;
    config_ =
        "global:\n"
        "  serverConnector:\n"
        "    addresses: ['Fake:42']\n"
        "consumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        persist_dir_;
    context_ = nullptr;
    context_mode_ = kShareContext;
  }

  virtual ~ContextFixture() {
    TestUtils::RemoveDir(log_dir_);
    TestUtils::RemoveDir(persist_dir_);
  }

  virtual void SetUp(::benchmark::State& state) {
    if (state.thread_index == 0) {
      if (context_ == nullptr) {
        std::string err_msg;
        Config* config = Config::CreateFromString(config_, err_msg);
        if (config == nullptr) {
          std::cout << "create config with error: " << err_msg << std::endl;
          abort();
        }
        context_ = Context::Create(config, context_mode_);
        delete config;
        if (context_ == nullptr) {
          std::cout << "create context with error: " << std::endl;
          abort();
        }
      }
    }
  }

  virtual void TearDown(::benchmark::State& state) {
    if (state.thread_index == 0) {
      if (context_ != nullptr && context_mode_ != kLimitContext) {
        delete context_;
      }
      context_ = nullptr;
    }
  }

  ReturnCode LoadData(v1::DiscoverResponse& response) {
    ServiceData* service_data = nullptr;
    ServiceDataNotify* notify = nullptr;
    LocalRegistry* local_registry = context_->GetLocalRegistry();

    ServiceKey service_key = {response.service().namespace_().value(), response.service().name().value()};
    service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
    ReturnCode ret_code =
        local_registry->LoadServiceDataWithNotify(service_key, service_data->GetDataType(), service_data, notify);
    if (ret_code != kReturnOk) {
      return ret_code;
    }
    return local_registry->UpdateServiceData(service_key, service_data->GetDataType(), service_data);
  }

 protected:
  std::string log_dir_;
  std::string persist_dir_;
  std::string config_;

  Context* context_;
  ContextMode context_mode_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_BENCHMARK_CONTEXT_FIXTURE_H_
