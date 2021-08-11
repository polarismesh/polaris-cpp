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

#include "monitor/service_record.h"

#include <gtest/gtest.h>
#include <pthread.h>

#include "mock/fake_server_response.h"
#include "polaris/model.h"
#include "test_utils.h"

namespace polaris {

class ServiceRecordTest : public ::testing::Test {
protected:
  virtual void SetUp() { service_record_ = new ServiceRecord(); }

  virtual void TearDown() {
    if (service_record_ != NULL) {
      delete service_record_;
      service_record_ = NULL;
    }
  }

protected:
  ServiceRecord *service_record_;
  std::vector<pthread_t> thread_list_;

  static void *UpdateService(void *args);
};

struct ThreadArgs {
  ServiceRecord *service_record_;
  bool stop_;
};

void *ServiceRecordTest::UpdateService(void *args) {
  ThreadArgs *thread_args = static_cast<ThreadArgs *>(args);
  v1::DiscoverResponse response;
  ServiceKey service_key = {"cpp_test", "cpp_test_service"};
  FakeServer::CreateServiceInstances(response, service_key, 100);
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  while (!thread_args->stop_) {
    thread_args->service_record_->ServiceDataUpdate(service_data);
  }
  service_data->DecrementRef();
  return NULL;
}

TEST_F(ServiceRecordTest, MultiThreadUpdate) {
  pthread_t tid;
  ThreadArgs args = {service_record_, false};
  for (int i = 0; i < 4; ++i) {
    pthread_create(&tid, NULL, UpdateService, &args);
    thread_list_.push_back(tid);
  }
  for (int i = 0; i < 10;) {
    std::map<ServiceKey, ::v1::ServiceInfo> report_data;
    service_record_->ReportServiceCache(report_data);
    if (report_data.empty()) {
      usleep(100);
    } else {
      ++i;
    }
  }
  args.stop_ = true;
  for (std::size_t i = 0; i < thread_list_.size(); ++i) {
    pthread_join(thread_list_[i], NULL);
  }
  thread_list_.clear();
}

}  // namespace polaris
