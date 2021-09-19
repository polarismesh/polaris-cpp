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

#include "plugin/health_checker/http_detector.h"

#include <gtest/gtest.h>
#include <pthread.h>

#include <string>

#include "mock/fake_net_server.h"
#include "model/model_impl.h"
#include "plugin/plugin_manager.h"
#include "test_utils.h"

namespace polaris {

class HttpHealthCheckerTest : public ::testing::Test {
protected:
  static void SetUpTestCase() {
    http_server_list_.push_back(
        NetServerParam(TestUtils::PickUnusedPort(), "HTTP/1.0 200 OK\r\n\r\n", kNetServerInit, 0));
    http_server_list_.push_back(NetServerParam(
        TestUtils::PickUnusedPort(), "HTTP/1.0 200 OK\r\nContent-Length: 10\r\n\r\n0123456789",
        kNetServerInit, 0));
    http_server_list_.push_back(NetServerParam(
        TestUtils::PickUnusedPort(), "HTTP/1.0 404 NOT FOUND\r\n\r\n", kNetServerInit, 0));
    http_server_list_.push_back(NetServerParam(
        TestUtils::PickUnusedPort(),
        "HTTP/1.0 404 NOT FOUND\r\nContent-Length: 10\r\n\r\n0123456789", kNetServerInit, 0));
    http_server_list_.push_back(
        NetServerParam(TestUtils::PickUnusedPort(), "HTTP/1.0 200 \r\n\r\n", kNetServerInit, 0));
    http_server_list_.push_back(
        NetServerParam(TestUtils::PickUnusedPort(), "HTTP/1.0 404 \r\n\r\n", kNetServerInit, 0));
    http_server_list_.push_back(
        NetServerParam(TestUtils::PickUnusedPort(), "HTTP/1.0\r\n\r\n", kNetServerInit, 0));
    http_server_list_.push_back(
        NetServerParam(TestUtils::PickUnusedPort(), "HTTP/1.0\r\n", kNetServerInit, 0));
    for (std::size_t i = 0; i < http_server_list_.size(); ++i) {
      pthread_create(&http_server_list_[i].tid_, NULL, FakeNetServer::StartTcp,
                     &http_server_list_[i]);
    }
    bool all_server_start = false;
    while (!all_server_start) {
      all_server_start = true;
      for (std::size_t i = 0; i < http_server_list_.size(); ++i) {
        if (http_server_list_[i].status_ == kNetServerInit) {
          all_server_start = false;
        } else {
          ASSERT_EQ(http_server_list_[i].status_, kNetServerStart);
        }
      }
      usleep(2000);
    }
  }

  static void TearDownTestCase() {
    for (std::size_t i = 0; i < http_server_list_.size(); ++i) {
      http_server_list_[i].status_ = kNetServerStop;
      pthread_join(http_server_list_[i].tid_, NULL);
    }
  }

  static std::vector<NetServerParam> http_server_list_;

  virtual void SetUp() {
    default_config_ = NULL;
    http_detector_  = new HttpHealthChecker();
  }

  virtual void TearDown() {
    if (default_config_ != NULL) {
      delete default_config_;
      default_config_ = NULL;
    }
    if (http_detector_ != NULL) {
      delete http_detector_;
      http_detector_ = NULL;
    }
  }

  void DetectingLocalPortCaseMap(const std::map<int, ReturnCode> &case_map) {
    DetectResult detect_result;
    for (std::map<int, ReturnCode>::const_iterator it = case_map.begin(); it != case_map.end();
         ++it) {
      Instance instance("instance_id", "0.0.0.0", it->first, 0);
      ASSERT_EQ(http_detector_->DetectInstance(instance, detect_result), it->second)
          << "port:" << it->first;
      ASSERT_EQ(detect_result.detect_type, kPluginHttpHealthChecker);
    }
  }

protected:
  HttpHealthChecker *http_detector_;
  Config *default_config_;
};

std::vector<NetServerParam> HttpHealthCheckerTest::http_server_list_;

TEST_F(HttpHealthCheckerTest, DetectInstanceResponseCode) {
  default_config_ = Config::CreateEmptyConfig();
  ASSERT_EQ(http_detector_->Init(default_config_, NULL), kReturnInvalidConfig);

  DetectResult detect_result;

  Instance instance_0("instance_id", "0.0.0.0", http_server_list_[0].port_, 1);
  ASSERT_EQ(http_detector_->DetectInstance(instance_0, detect_result), kReturnInvalidConfig);

  Instance instance_1("instance_id", "0.0.0.0", http_server_list_[1].port_, 1);
  ASSERT_EQ(http_detector_->DetectInstance(instance_1, detect_result), kReturnInvalidConfig);

  delete default_config_;
  std::string err_msg, content = "path:\n  /health";
  default_config_ = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(default_config_ != NULL && err_msg.empty());
  ASSERT_EQ(http_detector_->Init(default_config_, NULL), kReturnOk);

  std::map<int, ReturnCode> port_testing_case_map;
  port_testing_case_map[http_server_list_[0].port_]  = kReturnOk;
  port_testing_case_map[http_server_list_[1].port_]  = kReturnOk;
  port_testing_case_map[http_server_list_[2].port_]  = kReturnServerError;
  port_testing_case_map[http_server_list_[3].port_]  = kReturnServerError;
  port_testing_case_map[http_server_list_[4].port_]  = kReturnServerError;
  port_testing_case_map[http_server_list_[5].port_]  = kReturnServerError;
  port_testing_case_map[http_server_list_[6].port_]  = kReturnServerError;
  port_testing_case_map[http_server_list_[7].port_]  = kReturnServerError;
  port_testing_case_map[TestUtils::PickUnusedPort()] = kReturnNetworkFailed;
  port_testing_case_map[TestUtils::PickUnusedPort()] = kReturnNetworkFailed;
  DetectingLocalPortCaseMap(port_testing_case_map);
}

TEST_F(HttpHealthCheckerTest, DetectInstanceWithConfig) {
  std::string err_msg, content = "path:\n  /\ntimeout:\n  1000";
  default_config_ = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(default_config_ != NULL && err_msg.empty());
  ASSERT_EQ(http_detector_->Init(default_config_, NULL), kReturnOk);

  std::map<int, ReturnCode> port_testing_case_map;
  port_testing_case_map[http_server_list_[0].port_] = kReturnOk;
  port_testing_case_map[http_server_list_[1].port_] = kReturnOk;
  port_testing_case_map[http_server_list_[2].port_] = kReturnServerError;
  port_testing_case_map[http_server_list_[3].port_] = kReturnServerError;
  port_testing_case_map[http_server_list_[4].port_] = kReturnServerError;
  DetectingLocalPortCaseMap(port_testing_case_map);
}

TEST_F(HttpHealthCheckerTest, DetectInstanceTimeout) {
  std::string err_msg, content = "path:\n  /\ntimeout:\n  3";
  default_config_ = Config::CreateFromString(content, err_msg);
  POLARIS_ASSERT(default_config_ != NULL && err_msg.empty());
  ASSERT_EQ(http_detector_->Init(default_config_, NULL), kReturnOk);

  std::map<int, ReturnCode> port_testing_case_map;
  port_testing_case_map[http_server_list_[0].port_] = kReturnNetworkFailed;
  port_testing_case_map[http_server_list_[1].port_] = kReturnNetworkFailed;
  port_testing_case_map[http_server_list_[2].port_] = kReturnNetworkFailed;
  port_testing_case_map[http_server_list_[3].port_] = kReturnNetworkFailed;
  port_testing_case_map[http_server_list_[4].port_] = kReturnNetworkFailed;
  DetectingLocalPortCaseMap(port_testing_case_map);
}

}  // namespace polaris
