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

#include "api/c_api.h"
#include "polaris/polaris_api.h"

#include <gtest/gtest.h>

#include "logger.h"
#include "polaris/consumer.h"
#include "polaris/defs.h"
#include "polaris/log.h"
#include "provider/request.h"
#include "test_utils.h"
#include "utils/file_utils.h"

namespace polaris {

TEST(PolarisApiTest, SetLogger) {
  std::string log_dir;
  TestUtils::CreateTempDir(log_dir);
  polaris_set_log_dir(log_dir.c_str());
  POLARIS_LOG(POLARIS_INFO, "test test");
  POLARIS_STAT_LOG(POLARIS_INFO, "test test");
  EXPECT_TRUE(FileUtils::FileExists(log_dir + "/polaris.log"));
  EXPECT_TRUE(FileUtils::FileExists(log_dir + "/stat.log"));
  TestUtils::RemoveDir(log_dir);

  polaris_set_log_level(kPolarisLogLevelDebug);
  ASSERT_TRUE(GetLogger()->isLevelEnabled(kDebugLogLevel));
  ASSERT_FALSE(GetLogger()->isLevelEnabled(kTraceLogLevel));
}

TEST(PolarisApiTest, GetErrorMsg) {
  const char* msg = polaris_get_err_msg(static_cast<int>(kReturnInvalidArgument));
  ASSERT_TRUE(msg != NULL);
  ASSERT_TRUE(strlen(msg) > 0);
}

TEST(PolarisApiTest, CreateApi) {
  polaris_api* api = polaris_api_new();
  ASSERT_TRUE(api != NULL);
  polaris_api_destroy(&api);
}

TEST(PolarisApiTest, CreateApiFrom) {
  std::string config_file;
  ASSERT_EQ(TestUtils::CreateTempFile(config_file), true);
  polaris_api* api = polaris_api_new_from(config_file.c_str());
  FileUtils::RemoveFile(config_file);
  ASSERT_TRUE(api != NULL);
  polaris_api_destroy(&api);
}

TEST(PolarisApiTest, CreateApiFromContent) {
  std::string content;
  polaris_api* api = polaris_api_new_from_content(content.c_str());
  ASSERT_TRUE(api != NULL);  // 空字符串可以创建
  polaris_api_destroy(&api);

  content = "[,,,";
  api     = polaris_api_new_from_content(content.c_str());
  ASSERT_FALSE(api != NULL);  // 错误的字符串无法创建

  content =
      "global:\n"
      "  serverConnector:\n"
      "    addresses:\n"
      "      - 127.0.0.1:8081";
  api = polaris_api_new_from_content(content.c_str());
  ASSERT_TRUE(api != NULL);  // 有效配置可以创建
  polaris_api_destroy(&api);
}

TEST(PolarisApiTest, GetOneInstance) {
  polaris_get_one_instance_req* request = polaris_get_one_instance_req_new("Test", "cpp.test");
  polaris::GetOneInstanceRequestAccessor accessor(*request->request_);
  ASSERT_EQ(accessor.GetServiceKey().namespace_, "Test");
  ASSERT_EQ(accessor.GetServiceKey().name_, "cpp.test");

  polaris_get_one_instance_req_set_src_service_key(request, "Test2", "cpp.test2");
  polaris_get_one_instance_req_add_src_service_metadata(request, "key1", "value1");
  polaris_get_one_instance_req_add_src_service_metadata(request, "key2", "value2");
  polaris::ServiceInfo* src_service_info = accessor.GetSourceService();
  ASSERT_EQ(src_service_info->service_key_.namespace_, "Test2");
  ASSERT_EQ(src_service_info->service_key_.name_, "cpp.test2");
  ASSERT_EQ(src_service_info->metadata_.size(), 2);

  polaris_get_one_instance_req_set_hash_key(request, 123);
  ASSERT_EQ(accessor.GetCriteria().hash_key_, 123);
  polaris_get_one_instance_req_set_hash_string(request, "123");
  ASSERT_EQ(accessor.GetCriteria().hash_string_, "123");
  polaris_get_one_instance_req_set_ignore_half_open(request, true);
  ASSERT_EQ(accessor.GetCriteria().ignore_half_open_, true);
  polaris_get_one_instance_req_set_src_set_name(request, "test");
  ASSERT_EQ(src_service_info->metadata_.size(), 3);
  polaris_get_one_instance_req_set_timeout(request, 100);
  ASSERT_EQ(accessor.GetTimeout(), 100);

  polaris_get_one_instance_req_set_canary(request, "canary123");
  ASSERT_EQ(src_service_info->metadata_.size(), 4);

  polaris_get_one_instance_req_metadata_add_item(request, "m1", "v1");
  ASSERT_EQ(accessor.GetMetadataParam()->metadata_.size(), 1);
  polaris_get_one_instance_req_metadata_failover(request, kPolarisMetadataFailoverNotKey);
  ASSERT_EQ(accessor.GetMetadataParam()->failover_type_, kMetadataFailoverNotKey);

  polaris_get_one_instance_req_destroy(&request);
}

TEST(PolarisApiTest, GetInstances) {
  polaris_get_instances_req* request = polaris_get_instances_req_new("Test", "cpp.test");
  polaris::GetInstancesRequestAccessor accessor(*request->request_);
  ASSERT_EQ(accessor.GetServiceKey().namespace_, "Test");
  ASSERT_EQ(accessor.GetServiceKey().name_, "cpp.test");

  polaris_get_instances_req_set_src_service_key(request, "Test2", "cpp.test2");
  polaris_get_instances_req_add_src_service_metadata(request, "key1", "value1");
  polaris_get_instances_req_add_src_service_metadata(request, "key2", "value2");
  polaris::ServiceInfo* src_service_info = accessor.GetSourceService();
  ASSERT_EQ(src_service_info->service_key_.namespace_, "Test2");
  ASSERT_EQ(src_service_info->service_key_.name_, "cpp.test2");
  ASSERT_EQ(src_service_info->metadata_.size(), 2);

  polaris_get_instances_req_include_unhealthy(request, false);
  ASSERT_EQ(accessor.GetIncludeUnhealthyInstances(), false);
  polaris_get_instances_req_include_circuit_break(request, true);
  ASSERT_EQ(accessor.GetIncludeCircuitBreakerInstances(), true);
  polaris_get_instances_req_skip_route_filter(request, true);
  ASSERT_EQ(accessor.GetSkipRouteFilter(), true);

  polaris_get_instances_req_set_timeout(request, 100);
  ASSERT_EQ(accessor.GetTimeout(), 100);

  polaris_get_instances_req_set_canary(request, "canary123");
  ASSERT_EQ(src_service_info->metadata_.size(), 3);

  polaris_get_instances_req_destroy(&request);
}

TEST(PolarisApiTest, InstanceAccessor) {
  polaris_instance* instance = new polaris_instance();
  instance->is_ref_          = false;
  instance->instance_        = new polaris::Instance("1", "127.0.0.1", 80, 101);
  polaris::InstanceSetter setter(*instance->instance_);
  ASSERT_STREQ(polaris_instance_get_id(instance), "1");
  ASSERT_STREQ(polaris_instance_get_host(instance), "127.0.0.1");
  ASSERT_EQ(polaris_instance_get_port(instance), 80);
  ASSERT_EQ(polaris_instance_get_weight(instance), 101);
  setter.SetVpcId("vpc1");
  ASSERT_STREQ(polaris_instance_get_vpc_id(instance), "vpc1");
  setter.SetProtocol("p0");
  ASSERT_STREQ(polaris_instance_get_protocol(instance), "p0");
  setter.SetVersion("v2");
  ASSERT_STREQ(polaris_instance_get_version(instance), "v2");
  setter.SetPriority(1);
  ASSERT_EQ(polaris_instance_get_priority(instance), 1);
  setter.SetHealthy(false);
  ASSERT_EQ(polaris_instance_is_healthy(instance), false);
  setter.AddMetadataItem("key1", "value1");
  ASSERT_STREQ(polaris_instance_get_metadata(instance, "key1"), "value1");
  setter.SetLogicSet("abc");
  ASSERT_STREQ(polaris_instance_get_logic_set(instance), "abc");
  setter.SetRegion("a");
  setter.SetZone("b");
  setter.SetCampus("c");
  ASSERT_STREQ(polaris_instance_get_region(instance), "a");
  ASSERT_STREQ(polaris_instance_get_zone(instance), "b");
  ASSERT_STREQ(polaris_instance_get_campus(instance), "c");
  polaris_instance_destroy(&instance);
}

extern std::string g_test_persist_dir_;

class PolarisApiReqTest : public ::testing::Test {
protected:
  virtual void SetUp() {
    api_ = NULL;
    std::string content =
        "global:\n"
        "  api:\n"
        "  serverConnector:\n"
        "    addresses:\n"
        "      - 127.0.0.1:" +
        StringUtils::TypeToStr(TestUtils::PickUnusedPort()) +
        "\nconsumer:\n"
        "  localCache:\n"
        "    persistDir: " +
        g_test_persist_dir_;
    api_ = polaris_api_new_from_content(content.c_str());
    ASSERT_TRUE(api_ != NULL);
  }

  virtual void TearDown() {
    if (api_ != NULL) {
      polaris_api_destroy(&api_);
    }
  }

protected:
  polaris_api* api_;
};

TEST_F(PolarisApiReqTest, ConsumerApi) {
  polaris_get_one_instance_req* get_one_request =
      polaris_get_one_instance_req_new("Test", "c.api.test");
  polaris_instance* instance = NULL;
  ASSERT_TRUE(get_one_request != NULL);
  int ret = polaris_api_get_one_instance(api_, get_one_request, &instance);
  ASSERT_EQ(ret, kReturnTimeout);
  ASSERT_TRUE(instance == NULL);

  polaris_instances_resp* instances_resp = NULL;
  ret = polaris_api_get_one_instance_resp(api_, get_one_request, &instances_resp);
  ASSERT_EQ(ret, kReturnTimeout);
  ASSERT_TRUE(instances_resp == NULL);
  polaris_get_one_instance_req_destroy(&get_one_request);

  polaris_get_instances_req* instances_req = polaris_get_instances_req_new("Test", "c.api.test");
  ASSERT_TRUE(instances_req != NULL);
  ret = polaris_api_get_instances_resp(api_, instances_req, &instances_resp);
  ASSERT_EQ(ret, kReturnTimeout);
  ASSERT_TRUE(instances_resp == NULL);
  ret = polaris_api_get_all_instances(api_, instances_req, &instances_resp);
  ASSERT_EQ(ret, kReturnTimeout);
  ASSERT_TRUE(instances_resp == NULL);
  polaris_get_instances_req_destroy(&instances_req);
}

TEST_F(PolarisApiReqTest, UpdateCallResult) {
  polaris_service_call_result* call_result =
      polaris_service_call_result_new("Test", "service", "instance_id");

  polaris_service_call_result_set_ret_status(call_result, POLARIS_CALL_RET_ERROR);
  polaris_service_call_result_set_delay(call_result, 1000);
  polaris_service_call_result_set_ret_code(call_result, -1);
  ASSERT_EQ(polaris_api_update_service_call_result(api_, call_result), 0);

  polaris_service_call_result_destroy(&call_result);
}

TEST_F(PolarisApiReqTest, RegisterInstance) {
  polaris_register_instance_req* register_req =
      polaris_register_instance_req_new("Test", "c.api.cpp.test", "token", "127.0.0.1", 80);
  polaris_register_instance_req_set_vpc_id(register_req, "vpc1");
  polaris_register_instance_req_set_protocol(register_req, "tcp");
  polaris_register_instance_req_set_weight(register_req, 50);
  polaris_register_instance_req_set_priority(register_req, 1);
  polaris_register_instance_req_set_version(register_req, "v1");
  polaris_register_instance_req_add_metadata(register_req, "key1", "value1");
  polaris_register_instance_req_set_health_check_flag(register_req, true);
  polaris_register_instance_req_set_health_check_ttl(register_req, 8);
  v1::Instance* instance = register_req->request_->GetImpl().ToPb();
  ASSERT_EQ(instance->namespace_().value(), "Test");
  ASSERT_EQ(instance->service().value(), "c.api.cpp.test");
  ASSERT_EQ(instance->service_token().value(), "token");
  ASSERT_EQ(instance->host().value(), "127.0.0.1");
  ASSERT_EQ(instance->port().value(), 80);
  ASSERT_EQ(instance->vpc_id().value(), "vpc1");
  ASSERT_EQ(instance->protocol().value(), "tcp");
  ASSERT_EQ(instance->weight().value(), 50);
  ASSERT_EQ(instance->priority().value(), 1);
  ASSERT_EQ(instance->version().value(), "v1");
  ASSERT_EQ(instance->metadata_size(), 1);
  ASSERT_EQ(instance->health_check().type(), v1::HealthCheck::HEARTBEAT);
  ASSERT_EQ(instance->health_check().heartbeat().ttl().value(), 8);
  polaris_register_instance_req_set_timeout(register_req, 20);
  ASSERT_EQ(register_req->request_->GetImpl().GetTimeout(), 20);
  delete instance;

  int ret = polaris_api_register_instance(api_, register_req);
  ASSERT_EQ(ret, kReturnNetworkFailed);
  polaris_register_instance_req_destroy(&register_req);
}

TEST_F(PolarisApiReqTest, DeregisterInstance) {
  polaris_deregister_instance_req* deregister_req =
      polaris_deregister_instance_req_new("Test", "c.api.cpp.test", "token", "127.0.0.1", 80);
  polaris_deregister_instance_req_set_vpc_id(deregister_req, "vpc1");
  polaris_deregister_instance_req_set_timeout(deregister_req, 20);
  v1::Instance* instance = deregister_req->request_->GetImpl().ToPb();
  ASSERT_EQ(instance->namespace_().value(), "Test");
  ASSERT_EQ(instance->service().value(), "c.api.cpp.test");
  ASSERT_EQ(instance->service_token().value(), "token");
  ASSERT_EQ(instance->host().value(), "127.0.0.1");
  ASSERT_EQ(instance->port().value(), 80);
  ASSERT_EQ(instance->vpc_id().value(), "vpc1");
  ASSERT_EQ(deregister_req->request_->GetImpl().GetTimeout(), 20);
  delete instance;

  int ret = polaris_api_deregister_instance(api_, deregister_req);
  ASSERT_EQ(ret, kReturnNetworkFailed);
  polaris_deregister_instance_req_destroy(&deregister_req);
}

TEST_F(PolarisApiReqTest, InstanceHeartbeat) {
  polaris_instance_heartbeat_req* heartbeat_req =
      polaris_instance_heartbeat_req_new("Test", "c.api.cpp.test", "token", "127.0.0.1", 80);
  polaris_instance_heartbeat_req_set_vpc_id(heartbeat_req, "vpc1");
  polaris_instance_heartbeat_req_set_timeout(heartbeat_req, 20);

  v1::Instance* instance = heartbeat_req->request_->GetImpl().ToPb();
  ASSERT_EQ(instance->namespace_().value(), "Test");
  ASSERT_EQ(instance->service().value(), "c.api.cpp.test");
  ASSERT_EQ(instance->service_token().value(), "token");
  ASSERT_EQ(instance->host().value(), "127.0.0.1");
  ASSERT_EQ(instance->port().value(), 80);
  ASSERT_EQ(instance->vpc_id().value(), "vpc1");
  ASSERT_EQ(heartbeat_req->request_->GetImpl().GetTimeout(), 20);
  delete instance;

  ASSERT_EQ(polaris_api_instance_heartbeat(api_, heartbeat_req), kReturnNetworkFailed);
  polaris_instance_heartbeat_req_destroy(&heartbeat_req);
}

}  // namespace polaris
