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

#include <google/protobuf/util/time_util.h>
#include <gtest/gtest.h>

#include "integration/common/environment.h"
#include "integration/common/integration_base.h"
#include "logger.h"
#include "polaris/consumer.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

namespace polaris {

struct InstanceInfo {
  InstanceInfo(const std::string& token, const std::string& id) : service_token_(token), id_(id) {}
  std::string service_token_;
  std::string id_;
};

struct RouteRuleInfo {
  RouteRuleInfo(const std::string& token, const std::string& service)
      : service_token_(token), service_(service) {}
  std::string service_token_;
  std::string service_;
};

struct BreakerInfo {
  BreakerInfo(const std::string& cbid, const std::string& cbversion, const std::string& cbtoken,
              const std::string& token, const std::string& service, const std::string& cbname)
      : cbid_(cbid), cbversion_(cbversion), cbtoken_(cbtoken), token_(token), service_(service),
        cbname_(cbname) {}
  std::string cbid_;
  std::string cbversion_;
  std::string cbtoken_;
  std::string token_;
  std::string service_;
  std::string cbname_;
};

class SubsetRouteTest : public IntegrationBase {
protected:
  virtual void SetUp() {
    IntegrationBase::SetUp();

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
    ASSERT_TRUE((consumer_api_ = polaris::ConsumerApi::CreateFromString(content)) != NULL);

    //先创建2个服务，TmpA --> TmpB
    //测试都是假定流量从A到B，B创建带标签的实例，A的出规则创建subset路由规则
    serviceA_ = "cpp.subset_route_test.a" + StringUtils::TypeToStr(Time::GetCurrentTimeMs());
    serviceB_ = "cpp.subset_route_test.b" + StringUtils::TypeToStr(Time::GetCurrentTimeMs());
    v1::Service sa, sb;
    sa.mutable_name()->set_value(serviceA_);
    sb.mutable_name()->set_value(serviceB_);
    sa.mutable_namespace_()->set_value("Test");
    sb.mutable_namespace_()->set_value("Test");
    POLARIS_LOG(LOG_INFO, "Write subset test info: %s, %s %s", serviceA_.c_str(), serviceB_.c_str(),
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
    CreateService(sa, tokenA_);
    CreateService(sb, tokenB_);
  }

  virtual void TearDown() {
    //删除实例
    for (std::vector<InstanceInfo>::iterator it = created_ins.begin(); it != created_ins.end();
         it++) {
      DeletePolarisServiceInstance((*it).service_token_, (*it).id_);
    }
    //删除路由
    for (std::vector<RouteRuleInfo>::iterator it = created_routes.begin();
         it != created_routes.end(); it++) {
      DeletePolarisServiceRouteRule((*it).service_token_, (*it).service_, "Test");
    }
    //删除熔断
    for (std::vector<BreakerInfo>::iterator it = created_breakers.begin();
         it != created_breakers.end(); it++) {
      DeletePolarisSetBreakerRule((*it).cbname_, (*it).cbversion_, (*it).cbtoken_, "Test",
                                  (*it).token_, (*it).service_, "Test");
    }
    //删除
    DeleteService(serviceA_, "Test", tokenA_);
    DeleteService(serviceB_, "Test", tokenB_);
    delete consumer_api_;
    IntegrationBase::TearDown();
  }

  void AddOneInstance(std::map<std::string, std::string>& meta, const std::string& service,
                      const std::string& token, const std::string& host, int port, bool isolate) {
    std::string id;
    AddPolarisServiceInstance(service, "Test", token, host, port, meta, isolate, id);
    InstanceInfo ins(token, id);
    created_ins.push_back(ins);
    if (!id.empty() && instance_id_.empty()) {
      instance_id_ = id;
      std::cout << "use for update instance: " << instance_id_ << std::endl;
    }
  }

  void AddOneRouteRule(const std::string& path, const std::string& service,
                       const std::string& token) {
    //路由规则
    v1::Routing route;
    ParseMessageFromJsonFile(path, &route);
    route.mutable_service()->set_value(service);
    route.mutable_service_token()->set_value(token);
    AddPolarisRouteRule(route);
    RouteRuleInfo route_info(token, service);
    created_routes.push_back(route_info);
  }

  void AddOneBreaker(const std::string& cbversion, const std::string& path,
                     const std::string& service, const std::string& token) {
    v1::CircuitBreaker cb;
    std::string cbtoken;
    std::string cbid;
    ParseMessageFromJsonFile(path, &cb);
    cb.mutable_service()->set_value(service);
    cb.mutable_namespace_()->set_value("Test");

    uint32_t time_now = time(NULL);
    std::ostringstream oss;
    oss << time_now;
    std::string cbname = cb.name().value() + oss.str();
    cb.mutable_name()->set_value(cbname);
    AddPolarisSetBreakerRule(cb, token, cbversion, cbtoken, cbid);
    BreakerInfo breaker(cbid, cbversion, cbtoken, token, service, cbname);
    created_breakers.push_back(breaker);
  }

  void DoGetInstance(int total, std::map<std::string, int>& count,
                     polaris::GetOneInstanceRequest& request) {
    polaris::ReturnCode ret;
    polaris::InstancesResponse* response = NULL;
    //发起流量

    for (int i = 0; i < total; i++) {
      ret = consumer_api_->GetOneInstance(request, response);
      if (ret == polaris::kReturnOk) {
        const std::map<std::string, std::string>& subset             = response->GetSubset();
        std::map<std::string, std::string>::const_iterator subset_it = subset.find("set");
        ASSERT_TRUE(subset_it != subset.end());
        count["set:" + subset_it->second]++;
        delete response;
      }
    }
    printf("done get, size: %ld\n", count.size());
    for (std::map<std::string, int>::iterator it = count.begin(); it != count.end(); ++it) {
      printf("--> %s : %d \n", it->first.c_str(), it->second);
    }
  }

  void MakeBreaker(const std::string& dest_service, const std::string& ins,
                   const std::map<std::string, std::string>& subset,
                   const std::map<std::string, std::string>& labels,
                   polaris::ServiceKey& service_key, int total, float threshold, int waite,
                   polaris::GetOneInstanceRequest& request) {
    //制造熔断、保持、恢复
    //通过total，threshold，waite三个参数控制要制造的状态
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
    consumer_api_->UpdateServiceCallResult(result);
    sleep(5);
    int split = total * threshold + 1;
    for (int i = 0; i < 15; i++) {
      //均匀上报
      int count     = 0;
      int err_count = split;
      int suc_count = total - split;
      while (count++ < total) {
        int rand_num = rand() % total;
        if ((rand_num <= split && err_count > 0) || (rand_num > split && suc_count <= 0)) {
          result.SetRetCode(polaris::kCallRetError);
          result.SetRetStatus(polaris::kCallRetError);
          err_count--;
        } else {
          result.SetRetCode(polaris::kCallRetOk);
          result.SetRetStatus(polaris::kCallRetOk);
          suc_count--;
        }
        consumer_api_->UpdateServiceCallResult(result);
      }
      sleep(1);
    }
    //测试状态变化
    //一般来说，对于common_breaker这个配置，14秒熔断和保持，39秒恢复开始
    polaris::InstancesResponse* response = NULL;
    for (int j = 10; j < waite; j++) {
      consumer_api_->GetOneInstance(request, response);
      delete response;
      sleep(1);
    }
  }

public:
  static void AssertPercent(int total, float percent, float err_rate, int count) {
    // err_rate 0.8%, 0.008
    int dif = total * err_rate;
    ASSERT_TRUE(count > (total * percent - dif))
        << total << " " << percent << " " << err_rate << " " << count << " " << dif;
    ASSERT_TRUE(count < (total * percent + dif))
        << total << " " << percent << " " << err_rate << " " << count << " " << dif;
  }

protected:
  std::string tokenA_;
  std::string tokenB_;
  std::string serviceA_;
  std::string serviceB_;
  ConsumerApi* consumer_api_;
  //创建的实例、路由、熔断
  std::vector<InstanceInfo> created_ins;
  std::vector<RouteRuleInfo> created_routes;
  std::vector<BreakerInfo> created_breakers;
  //上报的实例id
  std::string instance_id_;
};

// // 同一服务下存在多个不同权重的SET，路由到该服务的请求可以按照权重进行分配
TEST_F(SubsetRouteTest, TestSubsetWeight) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());
  //创建3个subset
  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "s1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);
  meta["set"] = "s3";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50031, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50032, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/two_out_route_rule", serviceA_, tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";

  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "2";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);

  //测试比例
  AssertPercent(total, 1.0 / 6, 0.02, count["set:s1"]);
  AssertPercent(total, 2.0 / 6, 0.02, count["set:s2"]);
  AssertPercent(total, 3.0 / 6, 0.02, count["set:s3"]);
}

// // 路由规则中指定服务某个SET被隔离，则由到该SET的请求数为0
TEST_F(SubsetRouteTest, TestSubsetIsolate) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());

  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "s1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/two_out_route_rule", serviceA_, tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";

  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "5";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);

  ASSERT_TRUE(count["set:s1"] == 0);
  ASSERT_TRUE(count["set:s2"] > 0);
}

// // 服务下存在3个SET，seta和setb优先级相同，各占50%流量，setc优先级次之。
// // seta触发熔断，期望看到seta流量为0，setb与setc各占50%
TEST_F(SubsetRouteTest, TestSubsetBreaker) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());
  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "s1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);
  meta["set"] = "s3";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50031, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50032, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/two_out_route_rule", serviceA_, tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";

  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "2";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);

  ASSERT_TRUE(count["set:s1"] > 0);
  ASSERT_TRUE(count["set:s2"] > 0);
  ASSERT_TRUE(count["set:s3"] > 0);
  //通过上报控制熔断状态
  //熔断s2
  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  subset["set"] = "s2";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.5, 13, request);

  printf("after break! \n");
  //重新跑流量测试
  count.clear();
  DoGetInstance(total, count, request);

  ASSERT_TRUE(count["set:s1"] > 0);
  ASSERT_TRUE(count["set:s2"] == 0);
  ASSERT_TRUE(count["set:s3"] > 0);
}

// 服务存在2个SET，seta比setb优先级高，seta放量恢复中，放量20%。
// 期望，seta接收20%流量，setb接受80%流量
TEST_F(SubsetRouteTest, TestSubsetBreakerRecover) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());

  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "s1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);
  // meta["set"] = "s3";
  // AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50031, false);
  // AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50032, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/two_diffrent_weight_route_rule", serviceA_,
                  tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";

  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "2";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);

  ASSERT_TRUE(count["set:s1"] > 0);
  ASSERT_TRUE(count["set:s2"] == 0);
  //制造放量恢复
  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  subset["set"] = "s1";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.5, 40, request);
  // printf("after break! %s\n", bk.c_str());
  //重新跑流量测试
  count.clear();
  DoGetInstance(total, count, request);

  ASSERT_TRUE(count["set:s1"] > 0);
  ASSERT_TRUE(count["set:s2"] > 0);

  //测试比例
  AssertPercent(total, 1.0 / 9, 0.02, count["set:s1"]);
  AssertPercent(total, 8.0 / 9, 0.02, count["set:s2"]);
}

// // 服务下存在3个不同优先级的SET，最高优先级seta触发了熔断状态，
// // 次高优先级setb触发了保持状态，最低优先级setc状态正常，
// // 则路由到seta的请求由setc进行接管，路由到setb的请求保持不变
TEST_F(SubsetRouteTest, TestBreakerAndPreserved) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());
  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "s1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);
  meta["set"] = "s3";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50031, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50032, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/break_preserv_route_rule", serviceA_,
                  tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";

  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "2";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);
  ASSERT_TRUE(count["set:s1"] > 0);
  ASSERT_TRUE(count["set:s2"] == 0);
  ASSERT_TRUE(count["set:s3"] == 0);
  //通过上报控制熔断状态
  //制造放量恢复
  // s2保持 ，s1熔断
  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;

  subset["set"] = "s2";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.2, 13, request);
  subset["set"] = "s1";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.5, 13, request);

  printf("after break! ");
  //重新跑流量测试
  count.clear();
  DoGetInstance(total, count, request);
  ASSERT_TRUE(count["set:s1"] == 0);
  ASSERT_TRUE(count["set:s2"] == 0);
  ASSERT_TRUE(count["set:s3"] > 0);
}

//服务存在2个SET，seta比setb优先级高，seta触发熔断，setb触发保持，期望流量仍然路由到seta
TEST_F(SubsetRouteTest, TestBreakerAndPreserved2) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());

  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "s1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/break_preserv_route_rule", serviceA_,
                  tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";

  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "2";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);
  ASSERT_TRUE(count["set:s1"] > 0);
  ASSERT_TRUE(count["set:s2"] == 0);
  //通过上报控制熔断状态
  //制造放量恢复
  // s2保持 ，s1熔断
  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  subset["set"] = "s2";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.2, 13, request);
  subset["set"] = "s1";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.5, 13, request);

  printf("after break! \n");
  //重新跑流量测试
  count.clear();
  DoGetInstance(total, count, request);

  ASSERT_TRUE(count["set:s1"] > 0);
  ASSERT_TRUE(count["set:s2"] == 0);
}

//服务存在2个SET，seta比setb优先级高，seta被隔离，setb触发保持，期望流向seta的请求，返回路由失败错误
//这个没通过
TEST_F(SubsetRouteTest, TestIsolateAndPreserved) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());

  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "s1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);
  // meta["set"] = "s3";
  // AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50031, false);
  // AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50032, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/break_preserv_route_rule", serviceA_,
                  tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";

  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "5";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);
  ASSERT_TRUE(count.size() > 0);
  //通过上报控制熔断状态
  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  subset["set"] = "s2";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.2, 13, request);

  printf("after break! \n");
  //重新跑流量测试
  count.clear();
  DoGetInstance(total, count, request);

  // ASSERT_TRUE(count.size() == 0);
  ASSERT_TRUE(count["set:s1"] == 0);
  ASSERT_TRUE(count["set:s2"] == 0);
}

//服务下存在3个SET，seta和setb优先级相同，各占50%流量，setc优先级次之。
// seta放量恢复中，放量20%，期望看到seta占流量10%，setc占流量40%，setb占流量50%
TEST_F(SubsetRouteTest, TestWeightAndPreserved) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());

  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "s1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);
  meta["set"] = "s3";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50031, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50032, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/break_preserv_route_rule", serviceA_,
                  tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";

  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "3";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);
  ASSERT_TRUE(count["set:s1"] > 0);
  ASSERT_TRUE(count["set:s2"] > 0);
  ASSERT_TRUE(count["set:s3"] == 0);
  //通过上报控制熔断状态
  //制造放量恢复
  // ，s1恢复
  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  subset["set"] = "s1";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.5, 40, request);

  printf("after break! \n");
  //重新跑流量测试
  count.clear();
  DoGetInstance(total, count, request);

  ASSERT_TRUE(count["set:s1"] > 0);
  ASSERT_TRUE(count["set:s2"] > 0);
  ASSERT_TRUE(count["set:s3"] > 0);
  AssertPercent(total, 2.0 / 20, 0.02, count["set:s1"]);
}

// //整体SET触发熔断，期望所有发往这个set的流量（通过不同的接口），都切换到备份set
TEST_F(SubsetRouteTest, TestSetBreakAndBakup) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());

  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "s1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);
  meta["set"] = "s3";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50031, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50032, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/break_preserv_route_rule", serviceA_,
                  tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/set_level_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";

  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "2";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);
  ASSERT_TRUE(count["set:s1"] > 0);
  ASSERT_TRUE(count["set:s2"] == 0);
  ASSERT_TRUE(count["set:s3"] == 0);
  //通过labels熔断s1，期望流量都走s2
  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  subset["set"] = "s1";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.5, 13, request);

  printf("after break! \n");
  //重新跑流量测试
  count.clear();
  DoGetInstance(total, count, request);

  ASSERT_TRUE(count["set:s1"] == 0);
  ASSERT_TRUE(count["set:s2"] > 0);
  ASSERT_TRUE(count["set:s3"] == 0);
}

// 接口级熔断，对于具体接口触发的seta熔断，则对于该接口的流量，切换到备份set；
// 其他接口则继续路由到seta
TEST_F(SubsetRouteTest, TestSetBreakAndBakup2) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());

  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "s1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);
  meta["set"] = "s3";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50031, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50032, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/break_preserv_route_rule", serviceA_,
                  tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_labels_breaker", serviceB_,
                tokenB_);
  std::cout << "add breaker done!\n";

  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  sinfo.metadata_["num"]        = "2";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);
  ASSERT_TRUE(count["set:s1"] > 0);
  ASSERT_TRUE(count["set:s2"] == 0);
  ASSERT_TRUE(count["set:s3"] == 0);
  //接口级熔断,接口熔断不会熔断整个set
  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  subset["set"] = "s1";
  labels["num"] = "2";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.5, 13, request);

  printf("after break! \n");
  //重新跑流量测试
  count.clear();
  std::map<std::string, std::string> lbmap;
  lbmap["num"] = "2";
  request.SetLabels(lbmap);
  DoGetInstance(total, count, request);
  //期望流量到s2
  // ASSERT_TRUE(count.size() > 0);
  ASSERT_TRUE(count["set:s1"] == 0);
  ASSERT_TRUE(count["set:s2"] > 0);
  //测试其他label接口
  count.clear();
  lbmap["num"] = "3";
  request.SetLabels(lbmap);
  DoGetInstance(total, count, request);
  //期望流量仍然到s1
  // ASSERT_TRUE(count.size() > 0);
  ASSERT_TRUE(count["set:s1"] > 0);
  ASSERT_TRUE(count["set:s2"] == 0);
}

// 正则表达式场景，路由规则dst使用正则表达式匹配，匹配目标set为seta与setb，
// 其中seta被熔断，不存在降级set。期望：路由结果为setb
TEST_F(SubsetRouteTest, TestSetRegMatch) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());

  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "s1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);
  meta["set"] = "s3";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50031, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50032, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/reg_route_rule", serviceA_, tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";

  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  //使用路由规则的正则规则
  sinfo.metadata_["num"]        = "reg";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);
  ASSERT_TRUE(count["set:s2"] > 0);
  ASSERT_TRUE(count["set:s3"] > 0);
  //熔断s2
  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  subset["set"] = "s2";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.5, 13, request);

  printf("after break! \n");
  //重新跑流量测试
  count.clear();
  DoGetInstance(total, count, request);
  //期望流量到s3
  ASSERT_TRUE(count["set:s2"] == 0);
  ASSERT_TRUE(count["set:s3"] > 0);
}

// 正则表达式场景，路由规则dst使用正则表达式匹配，匹配目标set为seta与setb，
// 其中seta被熔断，存在降级setc。期望：路由结果为setb+setc
TEST_F(SubsetRouteTest, TestSetRegMatch2) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());

  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "t1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);
  meta["set"] = "s3";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50031, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50032, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/reg_route_rule", serviceA_, tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";

  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  //使用路由规则的正则规则
  sinfo.metadata_["num"]        = "reg2";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);
  ASSERT_TRUE(count["set:s2"] > 0);
  ASSERT_TRUE(count["set:t1"] == 0);
  //熔断s2
  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  subset["set"] = "s2";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.5, 13, request);

  printf("after break! \n");
  //重新跑流量测试
  count.clear();
  DoGetInstance(total, count, request);
  //期望流量到s3
  ASSERT_TRUE(count["set:s2"] == 0);
  ASSERT_TRUE(count["set:s3"] > 0);
}

//路由规则发生变更，则下一次路由采用新的路由规则进行
TEST_F(SubsetRouteTest, TestRuleUpdate) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());

  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "t1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);
  meta["set"] = "s3";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50031, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50032, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/reg_route_rule", serviceA_, tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";

  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  //使用路由规则的正则规则
  sinfo.metadata_["num"]        = "reg2";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);
  ASSERT_TRUE(count["set:s2"] > 0);
  ASSERT_TRUE(count["set:t1"] == 0);
  //更新路由规则
  v1::Routing route_up;
  ParseMessageFromJsonFile("test/integration/route/json_config/update_route_rule", &route_up);
  route_up.mutable_service()->set_value(serviceA_);
  route_up.mutable_service_token()->set_value(tokenA_);
  UpdatePolarisRouteRule(route_up);
  sleep(5);
  std::cout << "after update route rule\n";
  //重新跑流量测试
  count.clear();
  DoGetInstance(total, count, request);
  //期望流量到t1
  ASSERT_TRUE(count["set:s2"] == 0);
  ASSERT_TRUE(count["set:t1"] > 0);
}

// 正则表达式场景，路由规则dst使用正则表达式匹配，匹配目标set为seta与setb，其中seta被熔断后放量20%，
// 存在降级setc。期望：路由结果为：20%流量到seta+setb，80%流量到setc+setb
TEST_F(SubsetRouteTest, TestRegMatch3) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());

  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "t1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);
  meta["set"] = "s3";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50031, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50032, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/reg_route_rule2", serviceA_, tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";

  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  //使用路由规则的正则规则
  sinfo.metadata_["num"]        = "reg3";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);
  ASSERT_TRUE(count["set:s2"] > 0);
  ASSERT_TRUE(count["set:t1"] == 0);
  //熔断s2
  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  subset["set"] = "s2";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.5, 40, request);

  printf("after break! \n");

  //重新跑流量测试
  count.clear();
  DoGetInstance(total, count, request);
  //期望流量到t1
  ASSERT_TRUE(count["set:s2"] > 0);
  ASSERT_TRUE(count["set:t1"] > 0);
}

TEST_F(SubsetRouteTest, TestRegMatch4) {
  // 创建限流规则 A  ----> B
  printf("---> %s %s\n", tokenA_.c_str(), tokenB_.c_str());
  std::map<std::string, std::string> meta;
  //需要给每个subset创建2个实例，排除一些规则对subset流量的干扰
  meta["set"] = "t1";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50011, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50012, false);
  meta["set"] = "s2";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50021, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50022, false);
  meta["set"] = "s3";
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50031, false);
  AddOneInstance(meta, serviceB_, tokenB_, "127.0.0.1", 50032, false);

  //路由规则
  AddOneRouteRule("test/integration/route/json_config/reg_route_rule2", serviceA_, tokenA_);
  std::cout << "add route done!\n";
  //添加熔断规则, 目前只有被调规则, B是被调
  AddOneBreaker("v4", "test/integration/route/json_config/common_breaker", serviceB_, tokenB_);
  std::cout << "add breaker done!\n";
  //开始测试权重
  sleep(5);
  //准备请求
  std::map<std::string, int> count;
  polaris::ServiceKey service_key = {"Test", serviceB_};
  polaris::ServiceInfo sinfo;
  //使用路由规则的正则规则
  sinfo.metadata_["num"]        = "reg3";
  sinfo.service_key_.name_      = serviceA_;
  sinfo.service_key_.namespace_ = "Test";

  polaris::GetOneInstanceRequest request(service_key);
  request.SetSourceService(sinfo);
  //发起流量
  int total = 10000;
  DoGetInstance(total, count, request);

  //熔断s2
  std::map<std::string, std::string> subset;
  std::map<std::string, std::string> labels;
  subset["set"] = "s2";
  MakeBreaker(serviceB_, instance_id_, subset, labels, sinfo.service_key_, 60, 0.5, 13, request);

  count.clear();
  DoGetInstance(total, count, request);
}

}  // namespace polaris
