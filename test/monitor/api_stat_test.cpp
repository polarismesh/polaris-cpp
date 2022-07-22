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

#include "monitor/api_stat_registry.h"

#include <gtest/gtest.h>

#include "test_context.h"
#include "v1/request.pb.h"

namespace polaris {

class ApiStatTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    api_stat_registry_ = context_->GetContextImpl()->GetApiStatRegistry();
  }

  virtual void TearDown() {
    api_stat_registry_ = nullptr;
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
  }

 protected:
  Context* context_;
  ApiStatRegistry* api_stat_registry_;
};

TEST_F(ApiStatTest, ApiStatRecord) {
  for (int i = 0; i < 100; i++) {
    ApiStat api_stat(context_->GetContextImpl(), kApiStatConsumerGetOne);
    if (i % 2 == 0) {
      api_stat.Record(i % 4 == 0 ? kReturnServiceNotFound : kReturnServerError);
    }
  }
  google::protobuf::RepeatedField<v1::SDKAPIStatistics> statistics;
  api_stat_registry_->GetApiStatistics(statistics);
  // 1个接口  3种返回码 1个延迟范围
  ASSERT_EQ(statistics.size(), 1 * 3 * 1);
}

TEST_F(ApiStatTest, ApiStatReport) {
  for (int i = 0; i < 2000; i++) {
    int mod_i = i % 3;
    ReturnCode ret_code = mod_i == 0 ? kReturnOk : (mod_i == 1 ? kReturnServiceNotFound : kReturnServerError);
    api_stat_registry_->Record(kApiStatConsumerGetOne, ret_code, i % 1001);
  }
  google::protobuf::RepeatedField<v1::SDKAPIStatistics> statistics;
  api_stat_registry_->GetApiStatistics(statistics);
  // 1个接口  3种返回码 5个延迟范围
  ASSERT_EQ(statistics.size(), 1 * 3 * 7);

  // 再获取一次，无数据
  statistics.Clear();
  api_stat_registry_->GetApiStatistics(statistics);
  ASSERT_EQ(statistics.size(), 0);
}

}  // namespace polaris
