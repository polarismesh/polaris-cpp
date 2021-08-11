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

#include <errno.h>

#include "context_internal.h"
#include "mock/fake_server_response.h"
#include "polaris/consumer.h"
#include "polaris/context.h"
#include "polaris/limit.h"
#include "polaris/log.h"
#include "test_utils.h"
#include "v1/response.pb.h"

#include "v1/code.pb.h"

namespace polaris {
//分别测试10个实例，100个实例，500个实例，1000个实例下，性能情况
//又分别测试正常，熔断，隔离下的性能情况

void MakeBreaker(const std::string& dest_service, const std::string& ins,
                 const std::map<std::string, std::string>& subset,
                 const std::map<std::string, std::string>& labels, polaris::ServiceKey& service_key,
                 int total, float threhold, int waite, polaris::GetOneInstanceRequest& request,
                 polaris::InstancesResponse*& response, polaris::ConsumerApi* consumer) {
  //制造熔断、保持、恢复
  //通过total，thre，waite三个参数控制要制造的状态
  polaris::ServiceCallResult result;
  result.SetServiceNamespace("Test");
  result.SetServiceName(dest_service);
  result.SetInstanceId(ins);
  result.SetDelay(1);
  result.SetSubset(subset);
  result.SetLabels(labels);
  result.SetSource(service_key);
  result.SetRetCode(polaris::kCallRetError);
  result.SetRetStatus(polaris::kCallRetError);
  consumer->UpdateServiceCallResult(result);
  sleep(5);
  int thre = total * threhold + 4;

  for (int i = 0; i < thre; i++) {
    consumer->UpdateServiceCallResult(result);
  }
  result.SetRetCode(polaris::kCallRetOk);
  result.SetRetStatus(polaris::kCallRetOk);
  for (int i = thre; i < total; i++) {
    consumer->UpdateServiceCallResult(result);
  }
  sleep(3);
  //测试状态变化
  //一般来说，对于common_breaker这个配置，14秒熔断和保持，39秒恢复开始
  polaris::ReturnCode ret;
  for (int j = 0; j < waite; j++) {
    ret = consumer->GetOneInstance(request, response);
    sleep(1);
  }
}

class BM_SubSetRoute : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State& state) {
    if (state.thread_index != 0) {
      return;
    }
    // 设置Logger目录和日志级别
    TestUtils::CreateTempDir(log_dir_);
    // std::cout << "set log dir to " << log_dir_ << std::endl;
    SetLogDir(log_dir_);
    GetLogger()->SetLogLevel(kInfoLogLevel);

    service_key_.namespace_ = "benchmark_namespace";
    service_key_.name_      = "benchmark_service";
    //获取ip
    char* env = getenv("POLARIS_SERVER");
    if (env == NULL) {
      std::cout << "get env POLARIS_SERVER error " << errno << std::endl;
      exit(-1);
    }
    std::string polaris_server(env);

    // 创建Context
    TestUtils::CreateTempDir(persist_dir_);
    std::string err_msg, content =
                             "global:\n"
                             "  serverConnector:\n"
                             "    addresses: [" +
                             polaris_server + ":8081" +
                             "]\nconsumer:\n"
                             "  localCache:\n"
                             "    persistDir: " +
                             persist_dir_;
    Config* config = Config::CreateFromString(content, err_msg);
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
    ServiceContext* service_connext = context_->GetOrCreateServiceContext(service_key_);
    chain_                          = service_connext->GetServiceRouterChain();
    assert(chain_ != NULL);
    service_connext->DecrementRef();

    sleep(3);
    consumer_api_ = ConsumerApi::Create(context_);
    if (consumer_api_ == NULL) {
      std::cout << "create consumer_api_ failed" << std::endl;
      exit(-1);
    }
  }

  void TearDown(const ::benchmark::State& state) {
    // printf("done! %d \n", state.thread_index);
    if (state.thread_index != 0) {
      return;
    }
    chain_ = NULL;
    delete consumer_api_;
    consumer_api_ = NULL;
    context_      = NULL;
    if (context_ != NULL) {
      delete context_;
      context_ = NULL;
    }
    TestUtils::RemoveDir(log_dir_);
    TestUtils::RemoveDir(persist_dir_);

    //需要sleep一小会，否则server容易出错
    sleep(3);
  }

  ServiceKey service_key_;
  ServiceRouterChain* chain_;
  std::string persist_dir_;
  std::string log_dir_;
  Context* context_;

  /////
  std::string tokenA_;
  std::string tokenB_;
  std::string serviceA_;
  std::string serviceB_;
  ConsumerApi* consumer_api_;
  std::map<std::string, int> count;
};

BENCHMARK_DEFINE_F(BM_SubSetRoute, BM_SubSetRouteNum1000)
(benchmark::State& state) {
  ReturnCode ret_code;
  std::string serverA = "bilinBenchMarkA1000";
  std::string serverB = "bilinBenchMarkB1000";

  polaris::ServiceKey service_key = {"Test", serverB};
  polaris::GetOneInstanceRequest request(service_key);
  polaris::Instance instance;
  polaris::InstancesResponse* response = NULL;
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "reg3";
  sinfo.service_key_.name_      = serverA;
  sinfo.service_key_.namespace_ = "Test";
  request.SetSourceService(sinfo);

  int total  = 1;
  int64_t cc = 0;
  while (state.KeepRunning()) {
    if (consumer_api_ == NULL) {
      std::cout << "api NULL " << state.thread_index << std::endl;
      break;
    }
    if (consumer_api_->GetOneInstance(request, response) == polaris::kReturnOk) {
      delete response;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_SubSetRoute, BM_SubSetRouteNum1000)
    ->ThreadRange(1, 8)
    ->RangeMultiplier(2)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->UseRealTime();

BENCHMARK_DEFINE_F(BM_SubSetRoute, BM_SubSetRouteNum10)
(benchmark::State& state) {
  ReturnCode ret_code;
  std::string serverA = "bilinBenchMarkA10";
  std::string serverB = "bilinBenchMarkB10";

  polaris::ServiceKey service_key = {"Test", serverB};
  polaris::GetOneInstanceRequest request(service_key);
  polaris::Instance instance;
  polaris::InstancesResponse* response = NULL;
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "reg3";
  sinfo.service_key_.name_      = serverA;
  sinfo.service_key_.namespace_ = "Test";
  request.SetSourceService(sinfo);

  int total  = 1;
  int64_t cc = 0;
  while (state.KeepRunning()) {
    if (consumer_api_ == NULL) {
      std::cout << "api NULL " << state.thread_index << std::endl;
      break;
    }
    if (consumer_api_->GetOneInstance(request, response) == polaris::kReturnOk) {
      delete response;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_SubSetRoute, BM_SubSetRouteNum10)
    ->ThreadRange(1, 8)
    ->RangeMultiplier(2)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->UseRealTime();

BENCHMARK_DEFINE_F(BM_SubSetRoute, BM_SubSetRouteNum100)
(benchmark::State& state) {
  ReturnCode ret_code;
  std::string serverA = "bilinBenchMarkA100";
  std::string serverB = "bilinBenchMarkB100";

  polaris::ServiceKey service_key = {"Test", serverB};
  polaris::GetOneInstanceRequest request(service_key);
  polaris::Instance instance;
  polaris::InstancesResponse* response = NULL;
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "reg3";
  sinfo.service_key_.name_      = serverA;
  sinfo.service_key_.namespace_ = "Test";
  request.SetSourceService(sinfo);

  int total  = 1;
  int64_t cc = 0;
  while (state.KeepRunning()) {
    if (consumer_api_ == NULL) {
      std::cout << "api NULL " << state.thread_index << std::endl;
      break;
    }
    if (consumer_api_->GetOneInstance(request, response) == polaris::kReturnOk) {
      delete response;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_SubSetRoute, BM_SubSetRouteNum100)
    ->ThreadRange(1, 8)
    ->RangeMultiplier(2)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->UseRealTime();

BENCHMARK_DEFINE_F(BM_SubSetRoute, BM_SubSetRouteNum500)
(benchmark::State& state) {
  ReturnCode ret_code;
  std::string serverA = "bilinBenchMarkA500";
  std::string serverB = "bilinBenchMarkB500";

  polaris::ServiceKey service_key = {"Test", serverB};
  polaris::GetOneInstanceRequest request(service_key);
  polaris::Instance instance;
  polaris::InstancesResponse* response = NULL;
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "reg3";
  sinfo.service_key_.name_      = serverA;
  sinfo.service_key_.namespace_ = "Test";
  request.SetSourceService(sinfo);

  int total  = 1;
  int64_t cc = 0;
  while (state.KeepRunning()) {
    if (consumer_api_ == NULL) {
      std::cout << "api NULL " << state.thread_index << std::endl;
      break;
    }
    if (consumer_api_->GetOneInstance(request, response) == polaris::kReturnOk) {
      delete response;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_SubSetRoute, BM_SubSetRouteNum500)
    ->ThreadRange(1, 8)
    ->RangeMultiplier(2)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->UseRealTime();

BENCHMARK_DEFINE_F(BM_SubSetRoute, BM_SubSetRouteNum1000Break)
(benchmark::State& state) {
  ReturnCode ret_code;
  std::string serverA = "bilinBenchMarkA1000";
  std::string serverB = "bilinBenchMarkB1000";

  polaris::ServiceKey service_key = {"Test", serverB};
  polaris::GetOneInstanceRequest request(service_key);
  polaris::Instance instance;
  polaris::InstancesResponse* response = NULL;
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "reg3";
  sinfo.service_key_.name_      = serverA;
  sinfo.service_key_.namespace_ = "Test";
  request.SetSourceService(sinfo);
  if (state.thread_index == 0) {
    //熔断s2
    std::map<std::string, std::string> subset;
    std::map<std::string, std::string> labels;
    subset["set"] = "s2";
    if (consumer_api_ == NULL) {
      std::cout << "api NULL1 " << state.thread_index << std::endl;
      state.SetItemsProcessed(state.iterations());
      return;
    }
    MakeBreaker(serverB, "456tgb8980ik", subset, labels, sinfo.service_key_, 60, 0.4, 13, request,
                response, consumer_api_);
  }
  int total  = 1;
  int64_t cc = 0;
  while (state.KeepRunning()) {
    if (consumer_api_ == NULL) {
      std::cout << "api NULL " << state.thread_index << std::endl;
      break;
    }
    if (consumer_api_->GetOneInstance(request, response) == polaris::kReturnOk) {
      delete response;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_SubSetRoute, BM_SubSetRouteNum1000Break)
    ->ThreadRange(1, 8)
    ->RangeMultiplier(2)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->UseRealTime();

BENCHMARK_DEFINE_F(BM_SubSetRoute, BM_SubSetRouteNum1000Labels)
(benchmark::State& state) {
  ReturnCode ret_code;
  std::string serverA = "bilinBenchMarkA1000";
  std::string serverB = "bilinBenchMarkB1000";

  polaris::ServiceKey service_key = {"Test", serverB};
  polaris::GetOneInstanceRequest request(service_key);
  polaris::Instance instance;
  polaris::InstancesResponse* response = NULL;
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "reg3";
  sinfo.service_key_.name_      = serverA;
  sinfo.service_key_.namespace_ = "Test";
  request.SetSourceService(sinfo);
  if (state.thread_index == 0) {
    //熔断s2
    std::map<std::string, std::string> subset;
    std::map<std::string, std::string> labels;
    subset["set"] = "s2";
    if (consumer_api_ == NULL) {
      std::cout << "api NULL1 " << state.thread_index << std::endl;
      state.SetItemsProcessed(state.iterations());
      return;
    }
    MakeBreaker(serverB, "456tgb8980ik", subset, labels, sinfo.service_key_, 60, 0.4, 13, request,
                response, consumer_api_);
  }
  int total  = 1;
  int64_t cc = 0;
  //先构造labels
  std::vector<std::string> labes;
  int maxlen = 0xfff;
  for (int i = 0; i < maxlen + 10; i++) {
    std::ostringstream ss;
    ss << i;
    labes.push_back(ss.str());
  }
  std::map<std::string, std::string> lbmap;
  lbmap["num"] = "2";
  // request.SetLabels(lbmap);
  while (state.KeepRunning()) {
    if (consumer_api_ == NULL) {
      std::cout << "api NULL " << state.thread_index << std::endl;
      break;
    }
    cc &= maxlen;
    lbmap["num"] = labes[cc++];
    request.SetLabels(lbmap);
    if (consumer_api_->GetOneInstance(request, response) == polaris::kReturnOk) {
      delete response;
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_SubSetRoute, BM_SubSetRouteNum1000Labels)
    ->ThreadRange(1, 8)
    ->RangeMultiplier(2)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(10)
    ->UseRealTime();

}  // namespace polaris