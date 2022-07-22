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

#ifndef POLARIS_CPP_TEST_MOCK_MOCK_METRIC_CONNECTOR_H_
#define POLARIS_CPP_TEST_MOCK_MOCK_METRIC_CONNECTOR_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "metric/metric_connector.h"
#include "v1/code.pb.h"

namespace polaris {

class MockMetricConnector : public MetricConnector {
 public:
  MockMetricConnector(Reactor &reactor, Context *context)
      : MetricConnector(reactor, context), ignore_(false), ret_code_(kReturnOk) {
    response_.mutable_code()->set_value(v1::ExecuteSuccess);
  }

  virtual ~MockMetricConnector() {}

  MOCK_METHOD1(IsMetricInit, bool(v1::MetricKey *metric_key));

  MOCK_METHOD3(Initialize, ReturnCode(v1::MetricInitRequest *request, uint64_t timeout,
                                      grpc::RpcCallback<v1::MetricResponse> *callback));

  MOCK_METHOD3(Query, ReturnCode(v1::MetricQueryRequest *request, uint64_t timeout,
                                 grpc::RpcCallback<v1::MetricResponse> *callback));

  MOCK_METHOD3(Report, ReturnCode(v1::MetricRequest *request, uint64_t timeout,
                                  grpc::RpcCallback<v1::MetricResponse> *callback));

  template <typename T>
  void OnResponse(T *request, uint64_t, grpc::RpcCallback<v1::MetricResponse> *callback) {
    if (!ignore_) {
      if (ret_code_ == kReturnOk) {
        v1::MetricResponse *response = new v1::MetricResponse();
        response->CopyFrom(response_);
        callback->OnSuccess(response);
      } else {
        callback->OnError(ret_code_);
      }
    }
    delete request;
    delete callback;
  }

  template <typename T>
  void OnResponse500(T *request, uint64_t, grpc::RpcCallback<v1::MetricResponse> *callback) {
    if (!ignore_) {
      v1::MetricResponse *response = new v1::MetricResponse();
      response->CopyFrom(response_);
      response->mutable_code()->set_value(500001);
      callback->OnSuccess(response);
    }
    delete request;
    delete callback;
  }

  template <typename T>
  void OnResponse200(T *request, uint64_t, grpc::RpcCallback<v1::MetricResponse> *callback) {
    if (!ignore_) {
      v1::MetricResponse *response = new v1::MetricResponse();
      response->CopyFrom(response_);
      response->mutable_code()->set_value(v1::ExecuteSuccess);
      callback->OnSuccess(response);
    }
    delete request;
    delete callback;
  }

  bool ignore_;
  ReturnCode ret_code_;
  v1::MetricResponse response_;
};

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_MOCK_MOCK_METRIC_CONNECTOR_H_
