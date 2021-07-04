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

#include <gtest/gtest.h>

#include <pthread.h>

#include "model/location.h"
#include "utils/string_utils.h"

namespace polaris {

namespace model {

class ClientLocationTest : public ::testing::Test {
protected:
  virtual void SetUp() {}

  virtual void TearDown() {}

protected:
  ClientLocation client_location_;
};

TEST_F(ClientLocationTest, TestInit) {
  Location location;
  client_location_.Init(location);
  ASSERT_FALSE(client_location_.WaitInit(0));
  ASSERT_EQ(client_location_.GetVersion(), 0);

  location.region = "china";
  client_location_.Init(location);
  ASSERT_TRUE(client_location_.WaitInit(0));
  VersionedLocation versioned_location;
  client_location_.GetVersionedLocation(versioned_location);
  ASSERT_EQ(versioned_location.location_.region, location.region);
  ASSERT_EQ(versioned_location.location_.zone, location.zone);
  ASSERT_EQ(versioned_location.location_.campus, location.campus);
  ASSERT_EQ(versioned_location.version_, 1);
}

TEST_F(ClientLocationTest, TestUpdate) {
  Location location;
  client_location_.Init(location);
  ASSERT_FALSE(client_location_.WaitInit(0));

  client_location_.Update(location);
  ASSERT_TRUE(client_location_.WaitInit(0));

  location.region = "china";
  for (int i = 0; i < 4; i++) {
    client_location_.Update(location);
    VersionedLocation versioned_location;
    ASSERT_EQ(client_location_.GetVersion(), 1);
    client_location_.GetVersionedLocation(versioned_location);
    ASSERT_EQ(versioned_location.ToString(), "{region: china, zone: , campus: }_1");
  }
}

struct ThreadData {
  ClientLocation *client_location_;
  sync::Mutex begin_;
  int zone_id_;
};

void *UpdateLocation(void *args) {
  ThreadData *data = static_cast<ThreadData *>(args);
  data->begin_.Lock();
  int zone_id = ++data->zone_id_;
  data->begin_.Unlock();
  Location location;
  location.zone = StringUtils::TypeToStr<int>(zone_id);
  data->client_location_->Update(location);
  return NULL;
}

TEST_F(ClientLocationTest, MultiThreadTest) {
  ThreadData data;
  data.client_location_ = &client_location_;
  data.zone_id_         = 0;
  std::vector<pthread_t> thread_list;
  pthread_t tid;
  data.begin_.Lock();  // 先上锁，让新创建的所有线程等待
  for (int i = 0; i < 10; ++i) {
    pthread_create(&tid, NULL, UpdateLocation, &data);
    thread_list.push_back(tid);
  }
  data.begin_.Unlock();
  client_location_.WaitInit(1000);
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], NULL);
  }
  ASSERT_EQ(client_location_.GetVersion(), 10);
}

}  // namespace model
}  // namespace polaris
