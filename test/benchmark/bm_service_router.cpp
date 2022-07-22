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

#include "context/context_impl.h"
#include "mock/fake_server_response.h"
#include "polaris/context.h"
#include "polaris/log.h"
#include "test_utils.h"
#include "v1/response.pb.h"

namespace polaris {

class BM_ServiceRouter : public benchmark::Fixture {
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

    service_key_.namespace_ = "benchmark_namespace";
    service_key_.name_ = "benchmark_service";

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
    ServiceContext *service_connext = context_->GetContextImpl()->GetServiceContext(service_key_);
    chain_ = service_connext->GetServiceRouterChain();
    assert(chain_ != nullptr);
  }

  void TearDown(const ::benchmark::State &state) {
    if (state.thread_index != 0) {
      return;
    }
    chain_ = nullptr;
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
    TestUtils::RemoveDir(log_dir_);
    TestUtils::RemoveDir(persist_dir_);
  }

  ServiceKey service_key_;
  ServiceRouterChain *chain_;
  std::string persist_dir_;
  std::string log_dir_;
  Context *context_;
};

BENCHMARK_DEFINE_F(BM_ServiceRouter, PrepareRouteInfo)
(benchmark::State &state) {
  ReturnCode ret_code;
  if (state.thread_index == 0) {
    ret_code = FakeServer::InitService(context_->GetLocalRegistry(), service_key_, 1000, false);
    if (ret_code != kReturnOk) {
      state.SkipWithError("init service data failed");
      return;
    }
    Location location = {"华南", "深圳", "南山"};
    context_->GetContextImpl()->GetClientLocation().Update(location);
  }
  while (state.KeepRunning()) {
    RouteInfo route_info(service_key_, nullptr);
    ret_code = chain_->PrepareRouteInfo(route_info, 1000);
    if (ret_code != kReturnOk) {
      state.SkipWithError("get service data return error");
      break;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_ServiceRouter, PrepareRouteInfo)
    ->ThreadRange(1, 8)
    ->RangeMultiplier(2)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->UseRealTime();

BENCHMARK_DEFINE_F(BM_ServiceRouter, DoRoute)(benchmark::State &state) {
  ReturnCode ret_code;
  if (state.thread_index == 0) {
    ret_code = FakeServer::InitService(context_->GetLocalRegistry(), service_key_, state.range(0), false);
    if (ret_code != kReturnOk) {
      state.SkipWithError("init service data failed");
      return;
    }
    Location location = {"华南", "深圳", "南山"};
    context_->GetContextImpl()->GetClientLocation().Update(location);
  }
  while (state.KeepRunning()) {
    RouteInfo route_info(service_key_, nullptr);
    ret_code = chain_->PrepareRouteInfo(route_info, 1000);
    if (ret_code != kReturnOk) {
      state.SkipWithError("prepare service data return error");
      break;
    }
    RouteResult route_result;
    ret_code = chain_->DoRoute(route_info, &route_result);
    if (ret_code != kReturnOk) {
      state.SkipWithError("do route return error");
      break;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_ServiceRouter, DoRoute)
    ->ThreadRange(1, 8)
    ->Range(1, 10000)
    ->RangeMultiplier(10)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->UseRealTime();

}  // namespace polaris
