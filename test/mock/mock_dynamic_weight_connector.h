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

#ifndef POLARIS_CPP_TEST_MOCK_MOCK_DYNAMIC_WEIGHT_CONNECTOR_H_
#define POLARIS_CPP_TEST_MOCK_MOCK_DYNAMIC_WEIGHT_CONNECTOR_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <pthread.h>

#include <string>
#include <vector>

#include "context/context_impl.h"
#include "dynamicweight/dynamicweight_connector.h"

namespace polaris {

class MockDynamicWeightConnector : public DynamicWeightConnector {
 public:
  MOCK_METHOD2(Init, ReturnCode(Context* context, Config* config));

  MOCK_METHOD2(InstanceReportDynamicWeight, ReturnCode(const DynamicWeightRequest& req, uint64_t timeout_ms));

  MOCK_METHOD2(RegisterDynamicDataUpdateEvent, ReturnCode(const ServiceKey& service_key, uint64_t sync_interval));

  MOCK_METHOD0(HasCreateThread, bool());
};

// mock dynamic weight connector
DynamicWeightConnector* MockDynamicWeightCreator(void) {
  MockDynamicWeightConnector* connector = new MockDynamicWeightConnector();
  EXPECT_CALL(*connector, Init(::testing::_, ::testing::_)).Times(1).WillRepeatedly(::testing::Return(kReturnOk));
  EXPECT_CALL(*connector, HasCreateThread()).WillRepeatedly(::testing::Return(true));
  return connector;
}

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_MOCK_MOCK_DYNAMIC_WEIGHT_CONNECTOR_H_
