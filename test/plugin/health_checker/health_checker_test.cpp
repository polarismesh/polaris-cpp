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

#include "plugin/health_checker/health_checker.h"

#include <gtest/gtest.h>
#include <pthread.h>

#include <string>

#include "mock/fake_net_server.h"
#include "mock/mock_local_registry.h"
#include "model/model_impl.h"
#include "plugin/circuit_breaker/chain.h"
#include "plugin/plugin_manager.h"
#include "v1/response.pb.h"

namespace polaris {

class FakeCircuitBreakerChain : public CircuitBreakerChain {
 public:
  explicit FakeCircuitBreakerChain(const ServiceKey &service_key) : CircuitBreakerChain(service_key) {}
  virtual ~FakeCircuitBreakerChain() {}

  virtual bool TranslateStatus(const std::string &instance_id, CircuitBreakerStatus from_status,
                               CircuitBreakerStatus to_status) {
    std::map<std::string, CircuitBreakerStatus>::iterator iter = status_map_.find(instance_id);
    if (iter == status_map_.end()) {
      return false;
    }
    if (iter->second != from_status) {
      return false;
    }
    status_map_[instance_id] = to_status;
    return true;
  }

  std::map<std::string, CircuitBreakerStatus> &GetStatusMap() { return status_map_; }

 public:
  std::map<std::string, CircuitBreakerStatus> status_map_;
};

// 依赖上述测试用例启动的tcp、udp server
class HealthCheckerChainTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    std::string err_msg, content = "enable:\n  true";
    default_config_ = Config::CreateFromString(content, err_msg);
    ASSERT_TRUE(default_config_ != nullptr && err_msg.empty());
    service_key_.namespace_ = "test_service_namespace";
    service_key_.name_ = "test_service_name";
    local_registry_ = new MockLocalRegistry();
    circuit_breaker_chain_ = new FakeCircuitBreakerChain(service_key_);
    health_checker_chain_ = new HealthCheckerChainImpl(service_key_, local_registry_);
    ReturnCode ret = health_checker_chain_->Init(default_config_, nullptr);
    ASSERT_EQ(ret, kReturnOk);
  }

  virtual void TearDown() {
    if (default_config_ != nullptr) {
      delete default_config_;
      default_config_ = nullptr;
    }
    if (local_registry_ != nullptr) {
      delete local_registry_;
      local_registry_ = nullptr;
    }
    if (circuit_breaker_chain_ != nullptr) {
      delete circuit_breaker_chain_;
      circuit_breaker_chain_ = nullptr;
    }
    if (health_checker_chain_ != nullptr) {
      delete health_checker_chain_;
      health_checker_chain_ = nullptr;
    }
  }

 protected:
  Config *default_config_;
  ServiceKey service_key_;
  LocalRegistry *local_registry_;
  FakeCircuitBreakerChain *circuit_breaker_chain_;
  HealthCheckerChain *health_checker_chain_;
};

TEST_F(HealthCheckerChainTest, ChainDetectInstance) {
  // ASSERT_EQ(health_checker_chain_->GetHealthCheckers().size(), 1);  //
  // 只有tcp探测

  // v1::Response response;
  // // 只连接测试
  // Instance instance_8010("instance_8010", "127.0.0.1", 8010, 0);
  // instance_map.insert(std::make_pair(std::string("instance_8010"),
  // instance_8010));
  // Instance instance_8011("instance_8011", "127.0.0.1", 8011, 0);
  // instance_map.insert(std::make_pair(std::string("instance_8011"),
  // instance_8011));
  // Instance instance_8012("instance_8012", "127.0.0.1", 8012, 0);
  // instance_map.insert(std::make_pair(std::string("instance_8012"),
  // instance_8012));
  // Instance instance_8013("instance_8013", "127.0.0.1", 8013, 0);
  // instance_map.insert(std::make_pair(std::string("instance_8013"),
  // instance_8013));
  // Instance instance_8018("instance_8018", "127.0.0.1", 8018, 0);
  // instance_map.insert(std::make_pair(std::string("instance_8018"),
  // instance_8018));

  // ServiceData* service_data = ServiceData::CreateFromPb(&response,
  // kDataIsSyncing);
  // std::map<std::string, CircuitBreakerStatus>& status_map =
  // circuit_breaker_chain_->GetStatusMap();
  // status_map["instance_8010"] = kCircuitBreakerOpen;
  // status_map["instance_8011"] = kCircuitBreakerOpen;
  // status_map["instance_8012"] = kCircuitBreakerOpen;
  // status_map["instance_8013"] = kCircuitBreakerOpen;
  // status_map["instance_8018"] = kCircuitBreakerOpen;

  // CircuitBreakerData breaker_data;
  // breaker_data.version = time(nullptr);
  // breaker_data.open_instances.insert("instance_8010");
  // breaker_data.open_instances.insert("instance_8011");
  // breaker_data.open_instances.insert("instance_8012");
  // breaker_data.open_instances.insert("instance_8013");
  // breaker_data.open_instances.insert("instance_8018");
  // service->SetCircuitBreakerData(breaker_data);
  // sleep(11); // 间隔是10s,这里sleep 11s
  // ReturnCode retcode = health_checker_chain_->DetectInstance();
  // ASSERT_EQ(retcode, kReturnOk);

  // ASSERT_EQ(status_map["instance_8010"], kCircuitBreakerHalfOpen);
  // ASSERT_EQ(status_map["instance_8011"], kCircuitBreakerHalfOpen);
  // ASSERT_EQ(status_map["instance_8012"], kCircuitBreakerHalfOpen);
  // ASSERT_EQ(status_map["instance_8013"], kCircuitBreakerHalfOpen);
  // ASSERT_EQ(status_map["instance_8018"], kCircuitBreakerOpen);
}

}  // namespace polaris
