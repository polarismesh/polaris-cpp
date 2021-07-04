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

#ifndef POLARIS_CPP_TEST_MOCK_MOCK_LOCAL_REGISTRY_H_
#define POLARIS_CPP_TEST_MOCK_MOCK_LOCAL_REGISTRY_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "polaris/plugin.h"

namespace polaris {

class MockLocalRegistry : public LocalRegistry {
public:
  MOCK_METHOD2(Init, ReturnCode(Config *config, Context *context));

  MOCK_METHOD0(RunGcTask, void());

  MOCK_METHOD1(RemoveExpireServiceData, void(uint64_t current_time));

  MOCK_METHOD3(GetServiceDataWithRef,
               ReturnCode(const ServiceKey &service_key, ServiceDataType data_type,
                          ServiceData *&service_data));

  MOCK_METHOD4(LoadServiceDataWithNotify,
               ReturnCode(const ServiceKey &service_key, ServiceDataType data_type,
                          ServiceData *&service_data, ServiceDataNotify *&notify));

  MOCK_METHOD3(UpdateServiceData, ReturnCode(const ServiceKey &service_key,
                                             ServiceDataType data_type, ServiceData *service_data));

  MOCK_METHOD2(UpdateServiceSyncTime,
               ReturnCode(const ServiceKey &service_key, ServiceDataType data_type));

  MOCK_METHOD2(UpdateCircuitBreakerData,
               ReturnCode(const ServiceKey &service_key,
                          const CircuitBreakerData &circuit_breaker_data));

  MOCK_METHOD2(UpdateSetCircuitBreakerData,
               ReturnCode(const ServiceKey &service_key,
                          const CircuitBreakUnhealthySetsData &cb_unhealthy_set_data));

  MOCK_METHOD2(UpdateDynamicWeight, ReturnCode(const ServiceKey &service_key,
                                               const DynamicWeightData &dynamic_weight_data));

  MOCK_METHOD1(GetAllServiceKey, ReturnCode(std::set<ServiceKey> &service_key_set));

  void ExpectReturnData(std::vector<ReturnCode> return_code_list) {
    ::testing::Sequence s1;
    for (std::size_t i = 0; i < return_code_list.size(); ++i) {
      EXPECT_CALL(*this, GetServiceDataWithRef(::testing::_, ::testing::_, ::testing::_))
          .InSequence(s1)
          .WillOnce(::testing::DoAll(::testing::Invoke(this, &MockLocalRegistry::ReturnData),
                                     ::testing::Return(return_code_list[i])));
    }
    service_data_list_.clear();
    service_data_index_ = 0;
  }

  void ExpectReturnData(std::vector<ReturnCode> return_code_list, const ServiceKey &service_key) {
    ::testing::Sequence s1;
    for (std::size_t i = 0; i < return_code_list.size(); ++i) {
      EXPECT_CALL(*this, GetServiceDataWithRef(service_key, ::testing::_, ::testing::_))
          .InSequence(s1)
          .WillOnce(::testing::DoAll(::testing::Invoke(this, &MockLocalRegistry::ReturnData),
                                     ::testing::Return(return_code_list[i])));
    }
    service_data_list_.clear();
    service_data_index_ = 0;
  }

  void ReturnData(const ServiceKey & /*service_key*/, ServiceDataType /*data_type*/,
                  ServiceData *&service_data) {
    service_data = service_data_list_[service_data_index_++];
    if (service_data != NULL) {
      service_data->IncrementRef();
    }
  }
  std::vector<ServiceData *> service_data_list_;
  int service_data_index_;

  void ExpectReturnNotify(int times) {
    EXPECT_CALL(*this,
                LoadServiceDataWithNotify(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(::testing::Exactly(times))
        .WillRepeatedly(::testing::DoAll(::testing::Invoke(this, &MockLocalRegistry::ReturnNotify),
                                         ::testing::Return(kReturnOk)));
  }
  void ReturnNotify(const ServiceKey &service_key, ServiceDataType data_type,
                    ServiceData *& /*service_data*/, ServiceDataNotify *&notify) {
    notify = new ServiceDataNotify(service_key, data_type);
    service_notify_list_.push_back(notify);
  }
  std::vector<ServiceDataNotify *> service_notify_list_;
  void DeleteNotify() {
    for (std::size_t i = 0; i < service_notify_list_.size(); ++i) {
      delete service_notify_list_[i];
    }
    service_notify_list_.clear();
  }
};

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_MOCK_MOCK_LOCAL_REGISTRY_H_
