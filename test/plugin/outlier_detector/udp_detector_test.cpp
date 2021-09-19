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

#include "plugin/health_checker/udp_detector.h"

#include <gtest/gtest.h>
#include <pthread.h>

#include <string>

#include "mock/fake_net_server.h"
#include "model/model_impl.h"
#include "plugin/plugin_manager.h"
#include "test_utils.h"

namespace polaris {

class UdpHealthCheckerTest : public ::testing::Test {
protected:
  static void SetUpTestCase() {
    upd_server_list_.push_back(
        NetServerParam(TestUtils::PickUnusedPort(), "OK", kNetServerInit, 0));
    upd_server_list_.push_back(
        NetServerParam(TestUtils::PickUnusedPort(), "0x987654321", kNetServerInit, 0));
    upd_server_list_.push_back(NetServerParam(TestUtils::PickUnusedPort(), "", kNetServerInit, 0));
    upd_server_list_.push_back(
        NetServerParam(TestUtils::PickUnusedPort(), "0x123456789", kNetServerInit, 0));
    upd_server_list_.push_back(
        NetServerParam(TestUtils::PickUnusedPort(), "0x987654321", kNetServerInit, 0));
    for (std::size_t i = 0; i < upd_server_list_.size(); ++i) {
      pthread_create(&upd_server_list_[i].tid_, NULL, FakeNetServer::StartUdp,
                     &upd_server_list_[i]);
    }
    bool all_server_start = false;
    while (!all_server_start) {
      all_server_start = true;
      for (std::size_t i = 0; i < upd_server_list_.size(); ++i) {
        if (upd_server_list_[i].status_ == kNetServerInit) {
          all_server_start = false;
        } else {
          ASSERT_EQ(upd_server_list_[i].status_, kNetServerStart);
        }
      }
      usleep(2000);
    }
  }

  static void TearDownTestCase() {
    for (std::size_t i = 0; i < upd_server_list_.size(); ++i) {
      upd_server_list_[i].status_ = kNetServerStop;
      pthread_join(upd_server_list_[i].tid_, NULL);
    }
  }

  static std::vector<NetServerParam> upd_server_list_;

  virtual void SetUp() {
    default_config_ = NULL;
    udp_detector_   = new UdpHealthChecker();
  }

  virtual void TearDown() {
    if (default_config_ != NULL) {
      delete default_config_;
      default_config_ = NULL;
    }
    if (udp_detector_ != NULL) {
      delete udp_detector_;
      udp_detector_ = NULL;
    }
  }

  void DetectingLocalPortCaseMap(const std::map<int, ReturnCode> &case_map) {
    DetectResult detect_result;
    for (std::map<int, ReturnCode>::const_iterator it = case_map.begin(); it != case_map.end();
         ++it) {
      Instance instance("instance_id", "0.0.0.0", it->first, 0);
      ASSERT_EQ(udp_detector_->DetectInstance(instance, detect_result), it->second);
      ASSERT_EQ(detect_result.detect_type, kPluginUdpHealthChecker);
    }
  }

protected:
  UdpHealthChecker *udp_detector_;
  Config *default_config_;
};

std::vector<NetServerParam> UdpHealthCheckerTest::upd_server_list_;

TEST_F(UdpHealthCheckerTest, DetectInstanceResponseCode) {
  default_config_ = Config::CreateEmptyConfig();
  ASSERT_EQ(udp_detector_->Init(default_config_, NULL), kReturnInvalidConfig);

  std::map<int, ReturnCode> port_testing_case_map;
  port_testing_case_map[upd_server_list_[0].port_] = kReturnInvalidConfig;
  port_testing_case_map[upd_server_list_[1].port_] = kReturnInvalidConfig;
  DetectingLocalPortCaseMap(port_testing_case_map);

  delete default_config_;
  std::string err_msg, content = "send:\n  0x12345566";
  default_config_ = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(default_config_ != NULL && err_msg.empty());
  ASSERT_EQ(udp_detector_->Init(default_config_, NULL), kReturnOk);
  port_testing_case_map.clear();
  port_testing_case_map[upd_server_list_[0].port_]   = kReturnOk;
  port_testing_case_map[upd_server_list_[1].port_]   = kReturnOk;
  port_testing_case_map[upd_server_list_[2].port_]   = kReturnNetworkFailed;
  port_testing_case_map[TestUtils::PickUnusedPort()] = kReturnNetworkFailed;
  port_testing_case_map[TestUtils::PickUnusedPort()] = kReturnNetworkFailed;
  DetectingLocalPortCaseMap(port_testing_case_map);
}

TEST_F(UdpHealthCheckerTest, DetectInstanceWithConfig) {
  std::string err_msg, content =
                           "send:\n  0x12345678\n"
                           "receive:\n  0x4f4b\n"  // 0x4f4b为OK的二进制表示
                           "timeout:\n  1000";
  default_config_ = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(default_config_ != NULL && err_msg.empty());
  ASSERT_EQ(udp_detector_->Init(default_config_, NULL), kReturnOk);

  std::map<int, ReturnCode> port_testing_case_map;
  port_testing_case_map[upd_server_list_[0].port_] = kReturnOk;
  port_testing_case_map[upd_server_list_[1].port_] = kReturnServerError;
  port_testing_case_map[upd_server_list_[2].port_] = kReturnNetworkFailed;
  DetectingLocalPortCaseMap(port_testing_case_map);
}

TEST_F(UdpHealthCheckerTest, DetectInstanceWithTimeout) {
  std::string err_msg, content =
                           "send:\n  0x12345678\n"
                           "receive:\n  0x4f4b\n"  // 0x4f4b为OK的二进制表示
                           "timeout:\n  3";
  default_config_ = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(default_config_ != NULL && err_msg.empty());
  ASSERT_EQ(udp_detector_->Init(default_config_, NULL), kReturnOk);

  std::map<int, ReturnCode> port_testing_case_map;
  port_testing_case_map[upd_server_list_[0].port_] = kReturnNetworkFailed;
  port_testing_case_map[upd_server_list_[1].port_] = kReturnNetworkFailed;
  port_testing_case_map[upd_server_list_[2].port_] = kReturnNetworkFailed;
  DetectingLocalPortCaseMap(port_testing_case_map);
}

TEST_F(UdpHealthCheckerTest, DetectInstanceWithoutResponse) {
  std::string err_msg, content =
                           "send:\n  0x12345678\n"
                           "receive:\n  ''\n"  // 0x4f4b为OK的二进制表示
                           "timeout:\n  3";
  default_config_ = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(default_config_ != NULL && err_msg.empty());
  ASSERT_EQ(udp_detector_->Init(default_config_, NULL), kReturnOk);

  std::map<int, ReturnCode> port_testing_case_map;
  port_testing_case_map[upd_server_list_[0].port_] = kReturnNetworkFailed;
  port_testing_case_map[upd_server_list_[1].port_] = kReturnNetworkFailed;
  port_testing_case_map[upd_server_list_[2].port_] = kReturnNetworkFailed;
  DetectingLocalPortCaseMap(port_testing_case_map);
}

}  // namespace polaris
