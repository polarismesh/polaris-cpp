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

#include "plugin/load_balancer/hash/hash_manager.h"
#include "plugin/load_balancer/maglev/maglev.h"
#include "plugin/load_balancer/maglev/maglev_entry_selector.h"
#include "plugin/load_balancer/ringhash/continuum.h"
#include "plugin/load_balancer/ringhash/ringhash.h"
#include "plugin/load_balancer/weighted_random.h"
#include "polaris/context.h"
#include "utils/string_utils.h"
#include "v1/response.pb.h"

namespace polaris {

static ServiceData *CreateService(int instance_num, const ServiceKey &service_key) {
  v1::DiscoverResponse response;
  response.set_type(v1::DiscoverResponse::INSTANCE);
  v1::Service *service = response.mutable_service();
  service->mutable_namespace_()->set_value(service_key.namespace_);
  service->mutable_name()->set_value(service_key.name_);
  service->mutable_revision()->set_value("version");
  for (int i = 0; i < instance_num; i++) {
    ::v1::Instance *instance = response.mutable_instances()->Add();
    instance->mutable_id()->set_value("instance_" + StringUtils::TypeToStr<int>(i));
    instance->mutable_namespace_()->set_value(service_key.namespace_);
    instance->mutable_service()->set_value(service_key.name_);
    instance->mutable_host()->set_value("host" + StringUtils::TypeToStr<int>(i));
    instance->mutable_port()->set_value(i);
    instance->mutable_weight()->set_value(100);
  }
  return ServiceData::CreateFromPb(&response, kDataInitFromDisk);
}

class BM_LoadBalance : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State &state) {
    if (state.thread_index != 0) {
      return;
    }
    std::string err_msg;
    std::string content =
        "global:\n"
        "  serverConnector:\n"
        "    addresses:\n"
        "      - 127.0.0.1:8010";
    config_ = Config::CreateFromString(content, err_msg);
    if (config_ == NULL) {
      std::cout << "create config with error: " << err_msg << std::endl;
      exit(-1);
    }
    context_ = Context::Create(config_);
    if (context_ == NULL) {
      std::cout << "create context failed" << std::endl;
      delete config_;
      exit(-1);
    }
    ServiceKey service_key    = {"benchmark_namespace", "benchmark_service"};
    ServiceData *service_data = CreateService(state.range(0), service_key);
    service_instances_        = new ServiceInstances(service_data);
    service_                  = new Service(service_key, 0);
    service_->UpdateData(service_data);
    load_balancer_ = NULL;
  }

  void TearDown(const ::benchmark::State &state) {
    if (state.thread_index != 0) {
      return;
    }
    if (config_ != NULL) {
      delete config_;
    }
    if (context_ != NULL) {
      delete context_;
    }
    delete service_instances_;
    delete service_;
  }

  Service *service_;
  ServiceInstances *service_instances_;
  Config *config_;
  Context *context_;
  RandomLoadBalancer *load_balancer_;
};

BENCHMARK_DEFINE_F(BM_LoadBalance, RandomLB)(benchmark::State &state) {
  if (state.thread_index == 0) {
    load_balancer_ = new RandomLoadBalancer();
    load_balancer_->Init(config_, context_);
  }
  Criteria criteria;
  while (state.KeepRunning()) {
    Instance *next      = NULL;
    ReturnCode ret_code = load_balancer_->ChooseInstance(service_instances_, criteria, next);
    if (ret_code != kReturnOk) {
      state.SkipWithError("choose instance return error");
      break;
    }
  }
  if (state.thread_index == 0) {
    delete load_balancer_;
    load_balancer_ = NULL;
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_LoadBalance, RandomLB)
    ->ThreadRange(1, 8)
    ->RangeMultiplier(4)
    ->Range(4, 4 << 10)
    ->MinTime(2)
    ->UseRealTime();

class BM_LBSimple : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State &state) {
    if (state.thread_index != 0) {
      return;
    }
    SetupConfig();
    context_ = Context::Create(config_);
    if (context_ == NULL) {
      std::cout << "create context failed" << std::endl;
      delete config_;
      exit(-1);
    }
    ServiceKey service_key    = {"benchmark_namespace", "benchmark_service"};
    ServiceData *service_data = CreateService(state.range(0), service_key);
    service_instances_        = new ServiceInstances(service_data);
    HashManager::Instance().GetHashFunction("murmur3", hashFunc_);
    service_ = new Service(service_key, 0);
    service_->UpdateData(service_data);
    instances_set_ = NULL;
    selector_      = NULL;
    lb_            = NULL;
  }

  virtual void SetupConfig() {
    std::string err_msg, content =
                             "global:\n"
                             "  serverConnector:\n"
                             "    addresses: ['Fake:42']\n"
                             "consumer:\n"
                             "  loadBalancer:\n"
                             "    type: ringHash\n"
                             "    vnodeCount: 10";
    config_ = Config::CreateFromString(content, err_msg);
    if (config_ == NULL) {
      std::cout << "create config with error: " << err_msg << std::endl;
      exit(-1);
    }
  }

  virtual void BMNoKey(benchmark::State &state) {
    Criteria criteria;
    while (state.KeepRunning()) {
      Instance *next      = NULL;
      ReturnCode ret_code = lb_->ChooseInstance(service_instances_, criteria, next);
      if (ret_code != kReturnOk) {
        state.SkipWithError("choose instance return error");
        break;
      }
    }
    if (state.thread_index == 0) {
      delete lb_;
      lb_ = NULL;
    }
    state.SetItemsProcessed(state.iterations());
  }

  virtual void BMWithKey(benchmark::State &state) {
    Criteria criteria;
    while (state.KeepRunning()) {
      state.PauseTiming();
      std::string uuid   = Utils::Uuid();
      criteria.hash_key_ = hashFunc_(static_cast<const void *>(uuid.c_str()), uuid.size(), 0);
      state.ResumeTiming();
      Instance *next      = NULL;
      ReturnCode ret_code = lb_->ChooseInstance(service_instances_, criteria, next);
      if (ret_code != kReturnOk) {
        state.SkipWithError("choose instance return error");
        break;
      }
    }
    if (state.thread_index == 0) {
      delete lb_;
      lb_ = NULL;
    }
    state.SetItemsProcessed(state.iterations());
  }

  void TearDown(const ::benchmark::State &state) {
    if (state.thread_index != 0) {
      return;
    }
    if (config_ != NULL) {
      delete config_;
    }
    if (context_ != NULL) {
      delete context_;
    }
    delete service_instances_;
    delete service_;
  }

  Service *service_;
  ServiceInstances *service_instances_;
  InstancesSet *instances_set_;
  ContinuumSelector *selector_;
  Hash64Func hashFunc_;
  std::map<uint64_t, uint64_t> hashCache_;
  Config *config_;
  Context *context_;
  LoadBalancer *lb_;
};

BENCHMARK_DEFINE_F(BM_LBSimple, RingHash)(benchmark::State &state) {
  if (0 == state.thread_index) {
    instances_set_ = service_instances_->GetAvailableInstances();
    selector_      = new ContinuumSelector();
  }

  while (state.KeepRunning()) {
    if (!selector_->Setup(instances_set_, state.range(0), hashFunc_)) {
      state.SkipWithError("Setup return failure");
      break;
    }
  }
  if (0 == state.thread_index) {
    delete selector_;
    selector_ = NULL;
  }
  state.SetItemsProcessed(state.iterations());
}

// 这个不能多线程调用
BENCHMARK_REGISTER_F(BM_LBSimple, RingHash)
    ->Args({100})
    ->Args({256})
    ->Args({1024})
    ->Iterations(10)
    ->Unit(benchmark::kMillisecond)
    ->MinTime(2)
    ->UseRealTime();

BENCHMARK_DEFINE_F(BM_LBSimple, RingHashFast)(benchmark::State &state) {
  if (0 == state.thread_index) {
    instances_set_ = service_instances_->GetAvailableInstances();
    selector_      = new ContinuumSelector();
  }

  while (state.KeepRunning()) {
    if (!selector_->FastSetup(instances_set_, state.range(0), hashFunc_)) {
      state.SkipWithError("FastSetup return failure");
      break;
    }
  }
  if (0 == state.thread_index) {
    delete selector_;
    selector_ = NULL;
  }
  state.SetItemsProcessed(state.iterations());
}

// 这个不能多线程调用
BENCHMARK_REGISTER_F(BM_LBSimple, RingHashFast)
    ->Args({100})
    ->Args({256})
    ->Args({1024})
    ->Iterations(10)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

BENCHMARK_DEFINE_F(BM_LBSimple, CohashNoKey)(benchmark::State &state) {
  if (state.thread_index == 0) {
    lb_ = new KetamaLoadBalancer();
    lb_->Init(config_, context_);
  }

  BMNoKey(state);
}

BENCHMARK_REGISTER_F(BM_LBSimple, CohashNoKey)
    ->ThreadRange(1, 8)
    ->RangeMultiplier(4)
    ->Range(4, 4 << 10)
    ->MinTime(2)
    ->UseRealTime();

BENCHMARK_DEFINE_F(BM_LBSimple, CohashWithKey)(benchmark::State &state) {
  if (state.thread_index == 0) {
    lb_ = new KetamaLoadBalancer();
    lb_->Init(config_, context_);
  }

  BMWithKey(state);
}

BENCHMARK_REGISTER_F(BM_LBSimple, CohashWithKey)
    ->ThreadRange(1, 8)
    ->RangeMultiplier(4)
    ->Range(4, 4 << 10)
    ->MinTime(2)
    ->UseRealTime();

class BM_LBMaglev : public BM_LBSimple {
public:
  virtual void SetupConfig() {
    std::string err_msg, content =
                             "global:\n"
                             "  serverConnector:\n"
                             "    addresses: ['Fake:42']\n"
                             "consumer:\n"
                             "  loadBalancer:\n"
                             "    type: maglev\n";
    config_ = Config::CreateFromString(content, err_msg);
    if (config_ == NULL) {
      std::cout << "create config with error: " << err_msg << std::endl;
      exit(-1);
    }
  }

  MaglevEntrySelector *maglev_selector_;
};

BENCHMARK_DEFINE_F(BM_LBMaglev, BuildLookupTable)(benchmark::State &state) {
  if (0 == state.thread_index) {
    instances_set_   = service_instances_->GetAvailableInstances();
    maglev_selector_ = new MaglevEntrySelector();
  }

  while (state.KeepRunning()) {
    if (!maglev_selector_->Setup(instances_set_, state.range(0), hashFunc_)) {
      state.SkipWithError("Setup return failure");
      break;
    }
  }
  if (0 == state.thread_index) {
    delete maglev_selector_;
    maglev_selector_ = NULL;
  }
  state.SetItemsProcessed(state.iterations());
}

// 这个不能多线程调用
BENCHMARK_REGISTER_F(BM_LBMaglev, BuildLookupTable)
    ->Args({1121})
    ->Args({5209})
    ->Args({65537})
    ->Args({655373})
    ->Iterations(100)
    ->Unit(benchmark::kMillisecond)
    ->MinTime(2)
    ->UseRealTime();

BENCHMARK_DEFINE_F(BM_LBMaglev, CohashNoKey)(benchmark::State &state) {
  if (state.thread_index == 0) {
    lb_ = new MaglevLoadBalancer();
    lb_->Init(config_, context_);
  }

  BMNoKey(state);
}

BENCHMARK_REGISTER_F(BM_LBMaglev, CohashNoKey)
    ->ThreadRange(1, 8)
    ->RangeMultiplier(4)
    ->Range(4, 4 << 10)
    ->MinTime(2)
    ->UseRealTime();

BENCHMARK_DEFINE_F(BM_LBMaglev, CohashWithKey)(benchmark::State &state) {
  if (state.thread_index == 0) {
    lb_ = new MaglevLoadBalancer();
    lb_->Init(config_, context_);
  }

  BMWithKey(state);
}

BENCHMARK_REGISTER_F(BM_LBMaglev, CohashWithKey)
    ->ThreadRange(1, 8)
    ->RangeMultiplier(4)
    ->Range(4, 4 << 10)
    ->MinTime(2)
    ->UseRealTime();

}  // namespace polaris
