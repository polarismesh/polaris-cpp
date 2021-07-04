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

#include "integration/common/integration_base.h"
#include "utils/string_utils.h"
#include "utils/time_clock.h"

namespace polaris {

class CircuitBreakerTest : public IntegrationBase {
protected:
  virtual void SetUp() {
    service_.mutable_namespace_()->set_value("Test");
    service_.mutable_name()->set_value("circuit_breaker_test_" +
                                       StringUtils::TypeToStr(Time::GetCurrentTimeMs()));
    IntegrationBase::SetUp();
    CreateInstance(healthy_instance_, healthy_instance_id_, "127.0.0.1", 8080);
    CreateInstance(unhealthy_instance_, unhealthy_instance_id_, "127.0.0.1", 8081);

    sleep(3);
    // 创建Consumer对象
    consumer_ = polaris::ConsumerApi::Create(context_);
    ASSERT_TRUE(consumer_ != NULL);
  }

  virtual void TearDown() {
    if (consumer_ != NULL) {
      delete consumer_;
    }
    DeletePolarisServiceInstance(healthy_instance_);
    DeletePolarisServiceInstance(unhealthy_instance_);
    IntegrationBase::TearDown();
  }

  void CreateInstance(v1::Instance& instance, std::string& instance_id, const std::string& ip,
                      uint32_t port) {
    instance.mutable_namespace_()->set_value(service_.namespace_().value());
    instance.mutable_service()->set_value(service_.name().value());
    instance.mutable_service_token()->set_value(service_token_);
    instance.mutable_weight()->set_value(100);
    instance.mutable_host()->set_value(ip);
    instance.mutable_port()->set_value(port);
    AddPolarisServiceInstance(instance, instance_id);
  }

protected:
  polaris::ConsumerApi* consumer_;

  std::string healthy_instance_id_;
  v1::Instance healthy_instance_;
  std::string unhealthy_instance_id_;
  v1::Instance unhealthy_instance_;
};

TEST_F(CircuitBreakerTest, OpenCircuitbreakWhenContinueFailed) {
  ServiceKey service_key = {service_.namespace_().value(), service_.name().value()};
  polaris::GetOneInstanceRequest request(service_key);
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

  int healthy_count   = 0;
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

  sleep(30);  // 等待熔断状态到半开

  healthy_count   = 0;
  unhealthy_count = 0;
  call_result.SetRetStatus(kCallRetOk);
  for (int i = 0; i < 1000 && unhealthy_count <= 10; i++) {  // 最多测试10s
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

  healthy_count   = 0;
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
  EXPECT_TRUE(unhealthy_count_rate > 0.1) << unhealthy_count << std::endl;
}

}  // namespace polaris