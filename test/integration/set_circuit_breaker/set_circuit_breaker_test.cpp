//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
//  language governing permissions and limitations under the License.
//

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "polaris/consumer.h"

#include "integration/common/environment.h"
#include "integration/common/integration_base.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

namespace polaris {

struct ThreadArg {
  polaris::ConsumerApi* consumer;
  polaris::ServiceCallResult* result;
  int cnt;              // 发送多少秒
  uint64_t sleep_time;  // us
  uint32_t not_ok_rate;
  uint32_t not_ok_type;  //  1 --- err        2--- slow     3 --- specific error
};

class SetCircuitBreakerTest : public IntegrationBase {
protected:
  virtual void SetUp() {
    set1_                   = "set1";
    set2_                   = "set2";
    set3_                   = "set3";
    set_key_                = "k1";
    service_key_.namespace_ = "Test";
    service_key_.name_      = "set_cb_test_" + StringUtils::TypeToStr(Time::GetCurrentTimeMs());
    cb_namespace_           = "Test";
    cb_version_             = "version1";
    IntegrationBase::SetUp();
    // 创建Consumer对象
    std::string content =
        "global:\n"
        "  serverConnector:\n"
        "    addresses: [" +
        Environment::GetDiscoverServer() +
        "]\n  system:\n"
        "    metricCluster:\n"
        "      namespace: Polaris\n"
        "      service: polaris.metric\n"
        "consumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        Environment::GetPersistDir() +
        "\n  circuitBreaker:\n"
        "    setCircuitBreaker:\n"
        "      enable: true\n";
    consumer_ = polaris::ConsumerApi::CreateFromString(content);
    ASSERT_TRUE(consumer_ != NULL);

    SetUpServiceData();
  }

  virtual void TearDown() {
    if (consumer_ != NULL) {
      delete consumer_;
    }
    TearDownServiceData();
    IntegrationBase::TearDown();
  }

  void CreateInstance(v1::Instance& instance, std::string& ins_id, const std::string& ip,
                      uint32_t port, const std::string& set_name) {
    instance.mutable_namespace_()->set_value(service_key_.namespace_);
    instance.mutable_service()->set_value(service_key_.name_);
    instance.mutable_service_token()->set_value(service_token_);
    instance.mutable_weight()->set_value(100);
    instance.mutable_host()->set_value(ip);
    instance.mutable_port()->set_value(port);
    (*instance.mutable_metadata())["k1"] = set_name;
    AddPolarisServiceInstance(instance, ins_id);
  }

  void CreateRoute() {
    route_.mutable_namespace_()->set_value(service_key_.namespace_);
    route_.mutable_service()->set_value(service_key_.name_);
    v1::Route* r1 = route_.mutable_inbounds()->Add();

    v1::Source* s1 = r1->mutable_sources()->Add();
    s1->mutable_namespace_()->set_value("*");
    s1->mutable_service()->set_value("*");
    v1::MatchString ms1;
    ms1.mutable_value()->set_value("fv1");
    ms1.set_type(v1::MatchString_MatchStringType_EXACT);
    (*s1->mutable_metadata())["f"] = ms1;

    v1::Destination* d1 = r1->mutable_destinations()->Add();
    d1->mutable_namespace_()->set_value(service_key_.namespace_);
    d1->mutable_service()->set_value(service_key_.name_);
    v1::MatchString dms1;
    dms1.mutable_value()->set_value("set1");
    dms1.set_type(v1::MatchString_MatchStringType_EXACT);
    (*d1->mutable_metadata())["k1"] = dms1;
    d1->mutable_priority()->set_value(0);
    d1->mutable_weight()->set_value(100);

    v1::Destination* d2 = r1->mutable_destinations()->Add();
    d2->mutable_namespace_()->set_value(service_key_.namespace_);
    d2->mutable_service()->set_value(service_key_.name_);
    v1::MatchString dms2;
    dms2.mutable_value()->set_value("set2");
    dms2.set_type(v1::MatchString_MatchStringType_EXACT);
    (*d2->mutable_metadata())["k1"] = dms2;
    d2->mutable_priority()->set_value(1);
    d2->mutable_weight()->set_value(100);

    v1::Destination* d3 = r1->mutable_destinations()->Add();
    d3->mutable_namespace_()->set_value(service_key_.namespace_);
    d3->mutable_service()->set_value(service_key_.name_);
    v1::MatchString dms3;
    dms3.mutable_value()->set_value("set3");
    dms3.set_type(v1::MatchString_MatchStringType_EXACT);
    (*d3->mutable_metadata())["k1"] = dms3;
    d3->mutable_priority()->set_value(2);
    d3->mutable_weight()->set_value(100);
    route_.mutable_service_token()->set_value(service_token_);
    AddPolarisRouteRule(route_);
  }

  void CreateCbConfig() {
    cb_name_ = "TestCb_t1" + StringUtils::TypeToStr(Time::GetCurrentTimeMs());
    circuit_breaker_.mutable_service_namespace()->set_value(service_key_.namespace_);
    circuit_breaker_.mutable_service()->set_value(service_key_.name_);
    circuit_breaker_.mutable_name()->set_value(cb_name_);
    circuit_breaker_.mutable_namespace_()->set_value(cb_namespace_);
    v1::CbRule* rule          = circuit_breaker_.mutable_inbounds()->Add();
    v1::SourceMatcher* source = rule->mutable_sources()->Add();
    source->mutable_namespace_()->set_value("*");
    source->mutable_service()->set_value("*");
    v1::MatchString ms;
    ms.mutable_value()->set_value(".*");
    ms.set_type(v1::MatchString_MatchStringType_REGEX);
    (*source->mutable_labels())["l1"] = ms;
    v1::DestinationSet* dst           = rule->mutable_destinations()->Add();
    dst->mutable_namespace_()->set_value("*");
    dst->mutable_service()->set_value("*");
    (*dst->mutable_metadata())["k1"]     = ms;
    v1::CbPolicy_ErrRateConfig* err_rate = dst->mutable_policy()->mutable_errorrate();
    err_rate->mutable_enable()->set_value(true);
    err_rate->mutable_errorratetopreserved()->set_value(10);
    err_rate->mutable_errorratetoopen()->set_value(20);
    err_rate->mutable_requestvolumethreshold()->set_value(30);

    v1::CbPolicy_ErrRateConfig_SpecialConfig* sp_conf = err_rate->mutable_specials()->Add();
    sp_conf->mutable_type()->set_value("sp-err-1");
    google::protobuf::Int64Value* errv1 = sp_conf->mutable_errorcodes()->Add();
    errv1->set_value(1222);
    google::protobuf::Int64Value* errv1_1 = sp_conf->mutable_errorcodes()->Add();
    errv1_1->set_value(1122);
    sp_conf->mutable_errorratetoopen()->set_value(10);
    sp_conf->mutable_errorratetopreserved()->set_value(1);

    v1::CbPolicy_ErrRateConfig_SpecialConfig* sp_conf_2 = err_rate->mutable_specials()->Add();
    sp_conf_2->mutable_type()->set_value("sp-err-2");
    google::protobuf::Int64Value* errv2 = sp_conf_2->mutable_errorcodes()->Add();
    errv2->set_value(1223);
    sp_conf_2->mutable_errorratetoopen()->set_value(10);
    sp_conf_2->mutable_errorratetopreserved()->set_value(1);

    v1::CbPolicy_SlowRateConfig* slow_rate = dst->mutable_policy()->mutable_slowrate();
    slow_rate->mutable_enable()->set_value(true);
    slow_rate->mutable_maxrt()->set_seconds(1);
    slow_rate->mutable_slowratetopreserved()->set_value(10);
    slow_rate->mutable_slowratetoopen()->set_value(30);

    dst->mutable_metricwindow()->set_seconds(10);
    dst->mutable_metricprecision()->set_value(100);
    dst->mutable_updateinterval()->set_seconds(3);

    v1::RecoverConfig* r = dst->mutable_recover();
    r->mutable_sleepwindow()->set_seconds(20);
    google::protobuf::UInt32Value* re1 = r->mutable_requestrateafterhalfopen()->Add();
    re1->set_value(20);
    google::protobuf::UInt32Value* re2 = r->mutable_requestrateafterhalfopen()->Add();
    re2->set_value(40);
    AddPolarisSetBreakerRule(circuit_breaker_, service_token_, cb_version_, cb_token_, cb_id_);
  }

  void SetUpServiceData() {
    service_.mutable_namespace_()->set_value(service_key_.namespace_);
    service_.mutable_name()->set_value(service_key_.name_);
    CreateService(service_, service_token_);

    CreateInstance(instance1_, ins1_id_, "127.0.0.1", 12310, set1_);
    CreateInstance(instance1_1_, ins1_id_1_, "127.0.0.1", 12311, set1_);
    CreateInstance(instance2_, ins2_id_, "127.0.0.1", 12320, set2_);
    CreateInstance(instance2_1_, ins2_id_1_, "127.0.0.1", 12321, set2_);
    CreateInstance(instance3_, ins3_id_, "127.0.0.1", 12330, set3_);

    CreateRoute();  // add routing

    CreateCbConfig();  // add circuit_breaker
  }

  void UpdateCbPbConf() {
    DeletePolarisSetBreakerRule(cb_name_, cb_version_, cb_token_, cb_namespace_, service_token_,
                                service_key_.name_, service_key_.namespace_);
    cb_name_    = "TestCb_t2" + StringUtils::TypeToStr(Time::GetCurrentTimeMs());
    cb_version_ = "version2";
    circuit_breaker2_.mutable_service_namespace()->set_value(service_key_.namespace_);
    circuit_breaker2_.mutable_service()->set_value(service_key_.name_);
    circuit_breaker2_.mutable_name()->set_value(cb_name_);
    circuit_breaker2_.mutable_namespace_()->set_value(cb_namespace_);
    v1::CbRule* rule          = circuit_breaker2_.mutable_inbounds()->Add();
    v1::SourceMatcher* source = rule->mutable_sources()->Add();
    source->mutable_namespace_()->set_value("*");
    source->mutable_service()->set_value("*");
    v1::MatchString ms;
    ms.mutable_value()->set_value(".*");
    ms.set_type(v1::MatchString_MatchStringType_REGEX);
    (*source->mutable_labels())["l1"] = ms;
    v1::DestinationSet* dst           = rule->mutable_destinations()->Add();
    dst->mutable_namespace_()->set_value("*");
    dst->mutable_service()->set_value("*");
    (*dst->mutable_metadata())["k1"]     = ms;
    v1::CbPolicy_ErrRateConfig* err_rate = dst->mutable_policy()->mutable_errorrate();
    err_rate->mutable_enable()->set_value(true);
    err_rate->mutable_errorratetopreserved()->set_value(60);
    err_rate->mutable_errorratetoopen()->set_value(80);
    err_rate->mutable_requestvolumethreshold()->set_value(30);

    v1::CbPolicy_SlowRateConfig* slow_rate = dst->mutable_policy()->mutable_slowrate();
    slow_rate->mutable_enable()->set_value(true);
    slow_rate->mutable_maxrt()->set_seconds(1);
    slow_rate->mutable_slowratetopreserved()->set_value(10);
    slow_rate->mutable_slowratetoopen()->set_value(20);

    dst->mutable_metricwindow()->set_seconds(10);
    dst->mutable_metricprecision()->set_value(100);
    dst->mutable_updateinterval()->set_seconds(3);

    v1::RecoverConfig* r = dst->mutable_recover();
    r->mutable_sleepwindow()->set_seconds(20);
    google::protobuf::UInt32Value* re1 = r->mutable_requestrateafterhalfopen()->Add();
    re1->set_value(20);
    AddPolarisSetBreakerRule(circuit_breaker2_, service_token_, cb_version_, cb_token_, cb_id_);
  }

  void TearDownServiceData() {
    DeletePolarisServiceInstance(instance1_);
    DeletePolarisServiceInstance(instance1_1_);
    DeletePolarisServiceInstance(instance2_);
    DeletePolarisServiceInstance(instance2_1_);
    DeletePolarisServiceInstance(instance3_);
    DeletePolarisServiceRouteRule(route_);
    DeletePolarisSetBreakerRule(cb_name_, cb_version_, cb_token_, cb_namespace_, service_token_,
                                service_key_.name_, service_key_.namespace_);
  }

  static void* UpdateCallFunc(void* arg) {
    ThreadArg* real_args = static_cast<ThreadArg*>(arg);
    polaris::ReturnCode ret;
    if (real_args->cnt == 0) {
      return NULL;
    }
    int64_t cnt   = real_args->cnt * 1000000;
    int64_t count = 0;
    int num       = 0;
    while (true) {
      real_args->result->SetRetStatus(kCallRetOk);
      real_args->result->SetRetCode(0);
      real_args->result->SetDelay(100);
      if (num % real_args->not_ok_rate == 0) {
        if (real_args->not_ok_type == 1) {
          real_args->result->SetRetStatus(kCallRetError);
          real_args->result->SetRetCode(1);
        } else if (real_args->not_ok_type == 2) {
          real_args->result->SetRetStatus(kCallRetOk);
          real_args->result->SetDelay(1500);
        } else if (real_args->not_ok_type == 3) {
          real_args->result->SetRetStatus(kCallRetError);
          real_args->result->SetRetCode(1222);
        }
      }
      if ((ret = real_args->consumer->UpdateServiceCallResult(*(real_args->result))) !=
          polaris::kReturnOk) {
        std::cout << "update call result for instance with error:" << ret
                  << " msg:" << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
      }
      usleep(real_args->sleep_time);
      count += real_args->sleep_time;
      ++num;
      if (count > cnt) {
        break;
      }
    }
    return NULL;
  }

  void TryUpdateCall(polaris::ServiceCallResult& err_result) {
    polaris::ReturnCode ret;
    int count = 0;
    while (true) {
      if ((ret = consumer_->UpdateServiceCallResult(err_result)) != polaris::kReturnOk) {
        std::cout << "update call result for instance with error:" << ret
                  << " msg:" << polaris::ReturnCodeToMsg(ret).c_str() << std::endl;
      }
      sleep(1);
      ++count;
      if (count > 3) {
        break;
      }
    }
  }

  void TestCircuitBreaker(ThreadArg* arg, std::map<std::string, int>& set_count, int times) {
    pthread_t tids[2];
    for (int i = 0; i < 2; ++i) {
      pthread_create(&tids[i], NULL, UpdateCallFunc, static_cast<void*>(arg));
    }
    for (int i = 0; i < 2; ++i) {
      pthread_join(tids[i], NULL);
    }
    RunGetOneInstancesByTimes(set_count, times);
  }

  void RunGetOneInstancesByTimes(std::map<std::string, int>& set_count, int times) {
    polaris::GetOneInstanceRequest req(service_key_);
    polaris::ServiceInfo service_info;
    service_info.service_key_.name_      = "test2";
    service_info.service_key_.namespace_ = "Test";
    service_info.metadata_["f"]          = "fv1";
    req.SetSourceService(service_info);
    polaris::Instance instance;
    for (int i = 0; i < times; ++i) {
      ASSERT_EQ(consumer_->GetOneInstance(req, instance), kReturnOk);
      std::map<std::string, std::string>& metadata = instance.GetMetadata();
      ASSERT_TRUE(metadata.count(set_key_) > 0);
      set_count[metadata[set_key_]]++;
    }
  }

  static void AssertPercent(int total, float percent, float err_rate, int count) {
    // err_rate 0.8%, 0.008
    int dif = total * err_rate;
    ASSERT_TRUE(count > (total * percent - dif))
        << total << " " << percent << " " << err_rate << " " << count;
    ASSERT_TRUE(count < (total * percent + dif))
        << total << " " << percent << " " << err_rate << " " << count;
  }

protected:
  polaris::ServiceKey service_key_;
  polaris::ConsumerApi* consumer_;

  std::string set1_;
  std::string set2_;
  std::string set3_;
  std::string set_key_;

  std::string cb_namespace_;
  std::string cb_version_;
  std::string cb_token_;
  std::string cb_id_;
  std::string cb_name_;

  v1::Routing route_;
  v1::CircuitBreaker circuit_breaker_;
  v1::CircuitBreaker circuit_breaker2_;

  v1::Instance instance1_;
  std::string ins1_id_;
  v1::Instance instance1_1_;
  std::string ins1_id_1_;

  v1::Instance instance2_;
  std::string ins2_id_;
  v1::Instance instance2_1_;
  std::string ins2_id_1_;

  v1::Instance instance3_;
  std::string ins3_id_;
};

TEST_F(SetCircuitBreakerTest, TestRoute) {
  ReturnCode ret;
  polaris::GetOneInstanceRequest req(service_key_);
  polaris::ServiceInfo service_info;
  service_info.service_key_.name_      = "test2";
  service_info.service_key_.namespace_ = "Test";
  service_info.metadata_["f"]          = "fv1";
  req.SetSourceService(service_info);
  polaris::Instance instance;
  ret = consumer_->GetOneInstance(req, instance);
  ASSERT_EQ(ret, kReturnOk);
}

TEST_F(SetCircuitBreakerTest, ErrRateOpen) {
  polaris::ServiceCallResult err_result;
  err_result.SetServiceNamespace(service_key_.namespace_);
  err_result.SetServiceName(service_key_.name_);
  err_result.SetInstanceId(ins1_id_);

  std::map<std::string, std::string> subset;
  subset["k1"] = "set1";
  err_result.SetSubset(subset);

  std::map<std::string, std::string> labels;
  labels["l1"] = "v1";
  err_result.SetLabels(labels);

  polaris::ServiceKey source_service_key;
  source_service_key.name_      = "Test";
  source_service_key.namespace_ = "set_cb_sources_service";
  err_result.SetSource(source_service_key);
  err_result.SetRetCode(1);
  err_result.SetRetStatus(kCallRetError);

  TryUpdateCall(err_result);

  // 熔断set1
  ThreadArg arg;
  arg.result      = &err_result;
  arg.cnt         = 15;
  arg.sleep_time  = 1000 * 50;
  arg.consumer    = consumer_;
  arg.not_ok_type = 1;
  arg.not_ok_rate = 1;
  std::map<std::string, int> set_flags;
  TestCircuitBreaker(&arg, set_flags, 100);
  ASSERT_TRUE(set_flags.count(set1_) == 0);
  std::cout << "======================circuit breaker open test ok" << std::endl;

  // 半开放量
  printf("------------half open 1\n");
  sleep(35);
  arg.cnt = 0;
  set_flags.clear();
  TestCircuitBreaker(&arg, set_flags, 200);
  ASSERT_TRUE(set_flags.count(set1_) > 0);
  std::cout << "======================half open 1 test ok" << std::endl;

  // 进一步放量
  printf("------------half open 2\n");
  arg.result      = &err_result;
  arg.cnt         = 12;
  arg.sleep_time  = 1000 * 50;
  arg.not_ok_type = 1;
  arg.not_ok_rate = 0xfffffff;
  set_flags.clear();
  int total = 10000;
  TestCircuitBreaker(&arg, set_flags, total);
  ASSERT_TRUE(set_flags.count(set1_) > 0);
  AssertPercent(total, 0.4, 0.02, set_flags[set1_]);

  // 熔断
  sleep(2);
  arg.cnt         = 15;
  arg.not_ok_type = 1;
  arg.not_ok_rate = 1;
  set_flags.clear();
  TestCircuitBreaker(&arg, set_flags, 100);
  ASSERT_TRUE(set_flags.count(set1_) == 0);
}

TEST_F(SetCircuitBreakerTest, SlowRateOpen) {
  polaris::ServiceCallResult slow_result;
  slow_result.SetServiceNamespace(service_key_.namespace_);
  slow_result.SetServiceName(service_key_.name_);
  slow_result.SetInstanceId(ins1_id_);

  std::map<std::string, std::string> subset;
  subset["k1"] = "set1";
  slow_result.SetSubset(subset);

  std::map<std::string, std::string> labels;
  labels["l1"] = "v1";
  slow_result.SetLabels(labels);

  polaris::ServiceKey source_service_key;
  source_service_key.name_      = "Test";
  source_service_key.namespace_ = "set_cb_sources_service";
  slow_result.SetSource(source_service_key);
  slow_result.SetRetCode(1);
  slow_result.SetRetStatus(kCallRetOk);
  slow_result.SetDelay(1500);

  TryUpdateCall(slow_result);

  // 熔断set1
  ThreadArg arg;
  arg.result      = &slow_result;
  arg.cnt         = 15;
  arg.sleep_time  = 1000 * 50;
  arg.consumer    = consumer_;
  arg.not_ok_type = 2;
  arg.not_ok_rate = 1;
  std::map<std::string, int> set_flags;
  TestCircuitBreaker(&arg, set_flags, 100);
  ASSERT_TRUE(set_flags.count(set1_) == 0);
  std::cout << "======================circuit breaker open test ok" << std::endl;

  // 半开放量
  printf("------------half open 1\n");
  sleep(35);
  arg.cnt = 0;
  set_flags.clear();
  TestCircuitBreaker(&arg, set_flags, 200);
  ASSERT_TRUE(set_flags.count(set1_) > 0);
  std::cout << "======================half open 1 test ok" << std::endl;

  // 进一步放量
  printf("------------half open 2\n");
  arg.cnt         = 12;
  arg.sleep_time  = 1000 * 50;
  arg.not_ok_type = 2;
  arg.not_ok_rate = 0xfffffff;
  set_flags.clear();
  int total = 10000;
  TestCircuitBreaker(&arg, set_flags, total);
  ASSERT_TRUE(set_flags.count(set1_) > 0) << set_flags[set2_] << " " << set_flags[set3_];
  AssertPercent(total, 0.4, 0.02, set_flags[set1_]);

  // 熔断
  sleep(2);
  arg.cnt         = 15;
  arg.not_ok_type = 2;
  arg.not_ok_rate = 2;
  set_flags.clear();
  TestCircuitBreaker(&arg, set_flags, 100);
  ASSERT_TRUE(set_flags.count(set1_) == 0);
}

TEST_F(SetCircuitBreakerTest, ErrRatePreserved) {
  polaris::ServiceCallResult err_result;
  err_result.SetServiceNamespace(service_key_.namespace_);
  err_result.SetServiceName(service_key_.name_);
  err_result.SetInstanceId(ins1_id_);

  std::map<std::string, std::string> subset;
  subset["k1"] = "set1";
  err_result.SetSubset(subset);
  std::map<std::string, std::string> labels;
  labels["l1"] = "v1";
  err_result.SetLabels(labels);
  polaris::ServiceKey source_service_key;
  source_service_key.name_      = "Test";
  source_service_key.namespace_ = "set_cb_sources_service";
  err_result.SetSource(source_service_key);
  err_result.SetRetCode(1);
  err_result.SetRetStatus(kCallRetError);

  polaris::ServiceCallResult err_result2;
  err_result2.SetServiceNamespace(service_key_.namespace_);
  err_result2.SetServiceName(service_key_.name_);
  err_result2.SetInstanceId(ins2_id_);
  subset["k1"] = "set2";
  err_result2.SetSubset(subset);
  err_result2.SetLabels(labels);
  source_service_key.name_      = "Test";
  source_service_key.namespace_ = "set_cb_sources_service";
  err_result2.SetSource(source_service_key);
  err_result2.SetRetCode(1);
  err_result2.SetRetStatus(kCallRetError);

  TryUpdateCall(err_result);
  // 熔断set1, 保持set2
  ThreadArg arg;
  arg.result      = &err_result;
  arg.cnt         = 10;
  arg.sleep_time  = 1000 * 50;
  arg.consumer    = consumer_;
  arg.not_ok_type = 1;
  arg.not_ok_rate = 1;

  ThreadArg parg;
  parg.result      = &err_result2;
  parg.cnt         = 10;
  parg.consumer    = consumer_;
  parg.sleep_time  = 1000 * 50;
  parg.not_ok_type = 1;
  parg.not_ok_rate = 7;

  pthread_t tids[2], ptids[2];
  for (int i = 0; i < 1; ++i) {
    pthread_create(&tids[i], NULL, UpdateCallFunc, static_cast<void*>(&arg));
    pthread_create(&ptids[i], NULL, UpdateCallFunc, static_cast<void*>(&parg));
  }
  for (int i = 0; i < 1; ++i) {
    pthread_join(tids[i], NULL);
    pthread_join(ptids[i], NULL);
  }
  std::map<std::string, int> set_flags;
  RunGetOneInstancesByTimes(set_flags, 10);
  ASSERT_EQ(set_flags.size(), 1);
  ASSERT_TRUE(set_flags.count(set3_) > 0);
}

TEST_F(SetCircuitBreakerTest, SlowRatePreserved) {
  polaris::ServiceCallResult slow_result;
  slow_result.SetServiceNamespace(service_key_.namespace_);
  slow_result.SetServiceName(service_key_.name_);
  slow_result.SetInstanceId("e613c005285e32a6838823ce116cda99ea199f4d");
  std::map<std::string, std::string> subset;
  subset["k1"] = "set1";
  slow_result.SetSubset(subset);
  std::map<std::string, std::string> labels;
  labels["l1"] = "v1";
  slow_result.SetLabels(labels);
  polaris::ServiceKey source_service_key;
  source_service_key.name_      = "Test";
  source_service_key.namespace_ = "set_cb_sources_service";
  slow_result.SetSource(source_service_key);
  slow_result.SetRetCode(1);
  slow_result.SetRetStatus(kCallRetOk);

  polaris::ServiceCallResult slow_result2;
  slow_result2.SetServiceNamespace(service_key_.namespace_);
  slow_result2.SetServiceName(service_key_.name_);
  slow_result2.SetInstanceId("e613c005285e32a6838823ce116cda99ea199f4d");
  subset["k1"] = "set2";
  slow_result2.SetSubset(subset);
  slow_result2.SetLabels(labels);
  source_service_key.name_      = "Test";
  source_service_key.namespace_ = "set_cb_sources_service";
  slow_result2.SetSource(source_service_key);
  slow_result2.SetRetCode(1);
  slow_result2.SetRetStatus(kCallRetOk);

  TryUpdateCall(slow_result);
  // 熔断set1, 保持set2
  ThreadArg arg;
  arg.result      = &slow_result;
  arg.cnt         = 10;
  arg.sleep_time  = 1000 * 50;
  arg.consumer    = consumer_;
  arg.not_ok_type = 2;
  arg.not_ok_rate = 1;

  ThreadArg parg;
  parg.result      = &slow_result2;
  parg.cnt         = 10;
  parg.consumer    = consumer_;
  parg.sleep_time  = 1000 * 50;
  parg.not_ok_type = 2;
  parg.not_ok_rate = 7;

  pthread_t tids[2], ptids[2];
  for (int i = 0; i < 1; ++i) {
    pthread_create(&tids[i], NULL, UpdateCallFunc, static_cast<void*>(&arg));
    pthread_create(&ptids[i], NULL, UpdateCallFunc, static_cast<void*>(&parg));
  }
  for (int i = 0; i < 1; ++i) {
    pthread_join(tids[i], NULL);
    pthread_join(ptids[i], NULL);
  }
  std::map<std::string, int> set_flags;
  RunGetOneInstancesByTimes(set_flags, 100);
  ASSERT_EQ(set_flags.size(), 1);
  ASSERT_TRUE(set_flags.count(set3_) > 0);

  // 半开放量
  printf("------------half open 1\n");
  sleep(25);
  arg.cnt = 0;
  set_flags.clear();
  TestCircuitBreaker(&arg, set_flags, 100);
  ASSERT_TRUE(set_flags.count(set1_) > 0);
  std::cout << "======================half open 1 test ok" << std::endl;

  // 进一步放量
  printf("------------half open 2\n");
  sleep(5);
  arg.result      = &slow_result;
  arg.cnt         = 10;
  arg.sleep_time  = 1000 * 50;
  arg.not_ok_type = 1;
  arg.not_ok_rate = 0xfffffff;
  set_flags.clear();
  TestCircuitBreaker(&arg, set_flags, 100);
  ASSERT_TRUE(set_flags.count(set1_) > 0);
  std::cout << "======================half open 2 test ok" << std::endl;

  // 关闭熔断
  printf("------------half open 3\n");
  sleep(5);
  arg.cnt         = 10;
  arg.sleep_time  = 1000 * 50;
  arg.not_ok_type = 1;
  arg.not_ok_rate = 0xfffffff;
  set_flags.clear();
  TestCircuitBreaker(&arg, set_flags, 100);
  ASSERT_EQ(set_flags.size(), 1);
  ASSERT_TRUE(set_flags.count(set1_) > 0);
  std::cout << "======================circuit breaker close test done" << std::endl;
}

TEST_F(SetCircuitBreakerTest, CbConfUpdate) {
  polaris::ServiceCallResult err_result;
  err_result.SetServiceNamespace(service_key_.namespace_);
  err_result.SetServiceName(service_key_.name_);
  err_result.SetInstanceId(ins1_id_);

  std::map<std::string, std::string> subset;
  subset["k1"] = "set1";
  err_result.SetSubset(subset);

  std::map<std::string, std::string> labels;
  labels["l1"] = "v1";
  err_result.SetLabels(labels);

  polaris::ServiceKey source_service_key;
  source_service_key.name_      = "Test";
  source_service_key.namespace_ = "set_cb_sources_service";
  err_result.SetSource(source_service_key);
  err_result.SetRetCode(1);
  err_result.SetRetStatus(kCallRetError);

  TryUpdateCall(err_result);
  // 熔断set1
  ThreadArg arg;
  arg.result      = &err_result;
  arg.cnt         = 10;
  arg.sleep_time  = 1000 * 50;
  arg.consumer    = consumer_;
  arg.not_ok_type = 1;
  arg.not_ok_rate = 2;
  std::map<std::string, int> set_flags;
  TestCircuitBreaker(&arg, set_flags, 100);
  ASSERT_TRUE(set_flags.count(set1_) == 0);
  std::cout << "======================circuit breaker open test ok" << std::endl;

  // 半开放量
  printf("------------half open 1\n");
  sleep(25);
  arg.cnt = 0;
  set_flags.clear();
  TestCircuitBreaker(&arg, set_flags, 100);
  ASSERT_TRUE(set_flags.count(set1_) > 0);
  std::cout << "======================half open 1 test ok" << std::endl;

  // 进一步放量
  printf("------------half open 2\n");
  sleep(5);
  arg.result      = &err_result;
  arg.cnt         = 10;
  arg.sleep_time  = 1000 * 50;
  arg.not_ok_type = 1;
  arg.not_ok_rate = 0xfffffff;
  set_flags.clear();
  TestCircuitBreaker(&arg, set_flags, 100);
  ASSERT_TRUE(set_flags.count(set1_) > 0);
  std::cout << "======================half open 2 test ok" << std::endl;

  // 关闭熔断
  printf("------------half open 3\n");
  sleep(5);
  arg.result      = &err_result;
  arg.cnt         = 10;
  arg.sleep_time  = 1000 * 50;
  arg.not_ok_type = 1;
  arg.not_ok_rate = 0xfffffff;
  set_flags.clear();
  TestCircuitBreaker(&arg, set_flags, 100);
  ASSERT_EQ(set_flags.size(), 1);
  ASSERT_TRUE(set_flags.count(set1_) > 0);
  std::cout << "======================circuit breaker close test done" << std::endl;

  // 更新pb
  UpdateCbPbConf();

  TryUpdateCall(err_result);

  // 不熔断
  err_result.SetRetStatus(kCallRetError);
  err_result.SetDelay(0);
  err_result.SetRetCode(1);
  ThreadArg* arg2 = new ThreadArg();
  err_result.SetRetStatus(kCallRetOk);
  err_result.SetDelay(0);
  err_result.SetRetCode(0);
  arg2->result      = &err_result;
  arg2->cnt         = 15;
  arg2->sleep_time  = 1000 * 50;
  arg2->consumer    = consumer_;
  arg2->not_ok_type = 1;
  arg2->not_ok_rate = 2;
  set_flags.clear();
  TestCircuitBreaker(&arg, set_flags, 100);
  ASSERT_TRUE(set_flags.count(set1_) > 0);
  std::cout << "======================circuit breaker not open test done" << std::endl;
}

TEST_F(SetCircuitBreakerTest, SpecificError) {
  polaris::ServiceCallResult err_result;
  err_result.SetServiceNamespace(service_key_.namespace_);
  err_result.SetServiceName(service_key_.name_);
  err_result.SetInstanceId(ins1_id_);
  std::map<std::string, std::string> subset;
  subset["k1"] = "set1";
  err_result.SetSubset(subset);
  std::map<std::string, std::string> labels;
  labels["l1"] = "v1";
  err_result.SetLabels(labels);
  polaris::ServiceKey source_service_key;
  source_service_key.name_      = "Test";
  source_service_key.namespace_ = "set_cb_sources_service";
  err_result.SetSource(source_service_key);
  err_result.SetRetCode(1222);
  err_result.SetRetStatus(kCallRetError);

  polaris::ServiceCallResult err_result2;
  err_result2.SetServiceNamespace(service_key_.namespace_);
  err_result2.SetServiceName(service_key_.name_);
  err_result2.SetInstanceId(ins2_id_);
  subset["k1"] = "set2";
  err_result2.SetSubset(subset);
  err_result2.SetLabels(labels);
  source_service_key.name_      = "Test";
  source_service_key.namespace_ = "set_cb_sources_service";
  err_result2.SetSource(source_service_key);
  err_result2.SetRetCode(1222);
  err_result2.SetRetStatus(kCallRetError);

  TryUpdateCall(err_result);
  // 熔断set1, 保持set2
  ThreadArg arg;
  arg.result      = &err_result;
  arg.cnt         = 10;
  arg.sleep_time  = 1000 * 50;
  arg.consumer    = consumer_;
  arg.not_ok_type = 3;
  arg.not_ok_rate = 7;

  ThreadArg parg;
  parg.result      = &err_result2;
  parg.cnt         = 10;
  parg.consumer    = consumer_;
  parg.sleep_time  = 1000 * 50;
  parg.not_ok_type = 3;
  parg.not_ok_rate = 30;

  pthread_t tids[2], ptids[2];
  for (int i = 0; i < 1; ++i) {
    pthread_create(&tids[i], NULL, UpdateCallFunc, static_cast<void*>(&arg));
    pthread_create(&ptids[i], NULL, UpdateCallFunc, static_cast<void*>(&parg));
  }
  for (int i = 0; i < 1; ++i) {
    pthread_join(tids[i], NULL);
    pthread_join(ptids[i], NULL);
  }
  std::map<std::string, int> set_flags;
  RunGetOneInstancesByTimes(set_flags, 10);
  ASSERT_EQ(set_flags.size(), 1);
  ASSERT_TRUE(set_flags.count(set3_) > 0);
}

}  // namespace polaris