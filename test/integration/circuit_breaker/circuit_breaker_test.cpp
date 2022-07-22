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
#include "utils/time_clock.h"

namespace polaris {

const char kRouteTestNone[] = "none";      // 未启用set，未启用nearby
const char kRouteTestNearby[] = "nearby";  // 就近访问
const char kRouteTestSet[] = "set";        // set调用

const char kCalleeSetName[] = "app.sz.1";

class CircuitBreakerTest : public ::testing::TestWithParam<const char*> {
 protected:
  virtual void SetUp() {
    route_type_ = GetParam();
    if (route_type_ == kRouteTestNearby) {
      (*service_.mutable_metadata())["internal-enable-nearby"] = "true";
    }

    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("circuit_breaker_test_" + std::to_string(Time::GetSystemTimeMs()));
    std::string config_string =
        "global:\n"
        "  serverConnector:\n"
        "    addresses: [" +
        Environment::GetDiscoverServer() +
        "]\nconsumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        Environment::GetPersistDir() +
        "\n  serviceRouter:\n"
        "    chain:\n"
        "      - ruleBasedRouter\n"
        "      - setDivisionRouter\n"  // 此行用于通过配置开启set路由插件
        "      - nearbyBasedRouter\n"
        "  circuitBreaker:\n"
        "    plugin:\n"
        "      errorCount:\n"
        "        sleepWindow: 10000\n"
        "      errorRate:\n"
        "        sleepWindow: 10000";
    // 创建Consumer对象
    consumer_ = polaris::ConsumerApi::CreateFromString(config_string);
    ASSERT_TRUE(consumer_ != nullptr);

    IntegrationBase::CreateService(service_, service_token_);
    CreateInstance(healthy_instance_, healthy_instance_id_, "127.0.0.1", 8080);
    CreateInstance(unhealthy_instance_, unhealthy_instance_id_, "127.0.0.1", 8081);

    sleep(3);
  }

  virtual void TearDown() {
    if (consumer_ != nullptr) {
      delete consumer_;
    }
    IntegrationBase::DeletePolarisServiceInstance(healthy_instance_);
    IntegrationBase::DeletePolarisServiceInstance(unhealthy_instance_);
    IntegrationBase::DeleteService(service_.name().value(), service_.namespace_().value(), service_token_);
  }

  void CreateInstance(v1::Instance& instance, std::string& instance_id, const std::string& ip, uint32_t port) {
    instance.mutable_namespace_()->set_value(service_.namespace_().value());
    instance.mutable_service()->set_value(service_.name().value());
    instance.mutable_service_token()->set_value(service_token_);
    instance.mutable_weight()->set_value(100);
    instance.mutable_host()->set_value(ip);
    instance.mutable_port()->set_value(port);
    if (route_type_ == kRouteTestSet) {
      (*instance.mutable_metadata())["internal-set-name"] = kCalleeSetName;
    }
    IntegrationBase::AddPolarisServiceInstance(instance, instance_id);
  }

 protected:
  v1::Service service_;
  std::string service_token_;

  polaris::ConsumerApi* consumer_;

  std::string healthy_instance_id_;
  v1::Instance healthy_instance_;
  std::string unhealthy_instance_id_;
  v1::Instance unhealthy_instance_;
  const char* route_type_;
};

TEST_P(CircuitBreakerTest, OpenCircuitbreakWhenContinueFailed) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  polaris::GetOneInstanceRequest request(service_key);
  if (route_type_ == kRouteTestSet) {
    request.SetSourceSetName(kCalleeSetName);
  }
  polaris::Instance instance;
  ASSERT_EQ(consumer_->GetOneInstance(request, instance), kReturnOk);

  polaris::ServiceCallResult call_result;
  call_result.SetServiceNamespace(service_key.namespace_);
  call_result.SetServiceName(service_key.name_);
  call_result.SetInstanceId(unhealthy_instance_id_);
  call_result.SetDelay(50);
  call_result.SetRetStatus(kCallRetError);  // kCallRetOk  kCallRetError  kCallRetTimeout
  // 模拟连续上报10次调用失败
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(consumer_->UpdateServiceCallResult(call_result), kReturnOk);
  }
  sleep(1);

  int healthy_count = 0;
  int unhealthy_count = 0;
  const int calltimes = 100;
  // 查看后面调用到该故障节点的次数
  for (int i = 0; i < calltimes; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(request, instance), kReturnOk);
    if (instance.GetId() == healthy_instance_id_) {
      healthy_count++;
    } else {
      unhealthy_count++;
    }
  }
  // 预期:基本不会调用到unhealthy节点
  float unhealthy_count_rate = unhealthy_count * 1.0 / calltimes;
  EXPECT_TRUE(unhealthy_count_rate < 0.1) << unhealthy_count << std::endl;

  sleep(12);  // 等待熔断状态到半开

  healthy_count = 0;
  unhealthy_count = 0;
  call_result.SetRetStatus(kCallRetOk);
  for (int i = 0; i < 1500 && unhealthy_count <= 10; i++) {  // 最多测试10s
    ASSERT_EQ(consumer_->GetOneInstance(request, instance), kReturnOk);
    if (instance.GetId() == healthy_instance_id_) {
      healthy_count++;
    } else {
      unhealthy_count++;
    }
    EXPECT_EQ(consumer_->UpdateServiceCallResult(call_result), kReturnOk);
    usleep(10000);
  }
  ASSERT_EQ(unhealthy_count, 11);
  sleep(1);

  healthy_count = 0;
  unhealthy_count = 0;
  // 查看后面调用到该故障节点的次数
  for (int i = 0; i < calltimes; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(request, instance), kReturnOk);
    if (instance.GetId() == healthy_instance_id_) {
      healthy_count++;
    } else {
      unhealthy_count++;
    }
  }
  // 预期:被熔断的节点能够恢复
  unhealthy_count_rate = unhealthy_count * 1.0 / calltimes;
  EXPECT_TRUE(unhealthy_count_rate > 0.3) << unhealthy_count << std::endl;
}

TEST_P(CircuitBreakerTest, OpenCircuitbreakWhenOverloadErrorrate) {
  // 模拟服务熔断
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  polaris::GetOneInstanceRequest request(service_key);
  if (route_type_ == kRouteTestSet) {
    request.SetSourceSetName(kCalleeSetName);
  }
  polaris::Instance instance;
  ASSERT_EQ(consumer_->GetOneInstance(request, instance), kReturnOk);

  polaris::ServiceCallResult call_result;
  call_result.SetServiceNamespace(service_key.namespace_);
  call_result.SetServiceName(service_key.name_);
  call_result.SetInstanceId(unhealthy_instance_id_);
  call_result.SetDelay(50);
  // 模拟连续1分钟内调用数超过10次，没有连续失败，但是错误次数超过半数
  for (int i = 0; i < 30; i++) {
    if (i % 3 == 0) {
      call_result.SetRetStatus(kCallRetOk);
      EXPECT_EQ(consumer_->UpdateServiceCallResult(call_result), kReturnOk);
    } else {
      call_result.SetRetStatus(kCallRetError);
      EXPECT_EQ(consumer_->UpdateServiceCallResult(call_result), kReturnOk);
    }
  }

  sleep(2);

  int healthy_count = 0;
  int unhealthy_count = 0;
  const int calltimes = 100;
  // 查看后面调用到该故障节点的次数
  for (int i = 0; i < calltimes; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(request, instance), kReturnOk);
    if (instance.GetId() == healthy_instance_id_) {
      healthy_count++;
    } else {
      unhealthy_count++;
    }
  }
  // 预期:基本不会调用到unhealthy节点
  float unhealthy_count_rate = unhealthy_count * 1.0 / calltimes;
  EXPECT_TRUE(unhealthy_count_rate < 0.1) << unhealthy_count << std::endl;

  // 等待熔断节点到达半开状态
  sleep(12);

  healthy_count = 0;
  unhealthy_count = 0;
  // 至少执行210次，因为SDK控制了半开请求的占比是20:1
  for (int i = 0; i < 1500 && unhealthy_count <= 10; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(request, instance), kReturnOk);
    if (instance.GetId() == healthy_instance_id_) {
      healthy_count++;
      call_result.SetInstanceId(healthy_instance_id_);
      call_result.SetRetStatus(kCallRetOk);
      EXPECT_EQ(consumer_->UpdateServiceCallResult(call_result), kReturnOk);
    } else {
      unhealthy_count++;
      call_result.SetInstanceId(unhealthy_instance_id_);
      if (unhealthy_count % 6 == 0) {
        call_result.SetRetStatus(kCallRetError);
        EXPECT_EQ(consumer_->UpdateServiceCallResult(call_result), kReturnOk);
      } else {
        call_result.SetRetStatus(kCallRetOk);
        EXPECT_EQ(consumer_->UpdateServiceCallResult(call_result), kReturnOk);
      }
    }
    usleep(20000);
  }
  ASSERT_EQ(unhealthy_count, 11);
  sleep(1);

  healthy_count = 0;
  unhealthy_count = 0;
  // 查看后面调用到该故障节点的次数
  for (int i = 0; i < calltimes; i++) {
    ASSERT_EQ(consumer_->GetOneInstance(request, instance), kReturnOk);
    if (instance.GetId() == healthy_instance_id_) {
      healthy_count++;
    } else {
      unhealthy_count++;
    }
  }

  // 预期:被熔断的节点能够恢复
  unhealthy_count_rate = unhealthy_count * 1.0 / calltimes;
  EXPECT_TRUE(unhealthy_count_rate > 0.3) << unhealthy_count << std::endl;
}

INSTANTIATE_TEST_CASE_P(Test, CircuitBreakerTest, ::testing::Values(kRouteTestNone, kRouteTestNearby, kRouteTestSet));

}  // namespace polaris