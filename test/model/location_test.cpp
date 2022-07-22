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
  client_location_.Init(location, true);
  ASSERT_FALSE(client_location_.WaitInit(0));
  ASSERT_EQ(client_location_.GetVersion(), 0);

  location.region = "china";
  client_location_.Init(location, true);
  ASSERT_TRUE(client_location_.WaitInit(0));
  Location get_location;
  client_location_.GetLocation(get_location);
  ASSERT_EQ(get_location.region, location.region);
  ASSERT_EQ(get_location.zone, location.zone);
  ASSERT_EQ(get_location.campus, location.campus);
  ASSERT_EQ(client_location_.GetVersion(), 1);
}

TEST_F(ClientLocationTest, TestWaitInitWhenDisableUpdate) {
  Location location;
  client_location_.Init(location, false);
  ASSERT_TRUE(client_location_.WaitInit(0));
  ASSERT_EQ(client_location_.GetVersion(), 0);
}

TEST_F(ClientLocationTest, TestUpdate) {
  Location location;
  client_location_.Init(location, true);
  ASSERT_FALSE(client_location_.WaitInit(0));

  client_location_.Update(location);
  ASSERT_TRUE(client_location_.WaitInit(0));

  Location got_location;
  // 相同的位置信息不会更新
  location.region = "china";
  for (int i = 0; i < 4; i++) {
    client_location_.Update(location);
    uint32_t version;
    ASSERT_EQ(client_location_.GetVersion(), 1);
    client_location_.GetLocation(got_location, version);
    ASSERT_EQ(version, 1);
    ASSERT_EQ(ClientLocation::ToString(got_location, version), "{region: china, zone: , campus: }_1");
  }

  // 不同的位置信息会更新
  location.zone = "beijing";
  client_location_.Update(location);
  ASSERT_EQ(client_location_.GetVersion(), 2);
  client_location_.GetLocation(got_location);
  ASSERT_EQ(got_location.ToString(), "{region: china, zone: beijing, campus: }");

  client_location_.Init(location, false);  // 禁止更新
  ASSERT_EQ(client_location_.GetVersion(), 3);
  location.zone = "guangzhou";
  client_location_.Update(location);
  ASSERT_EQ(client_location_.GetVersion(), 3);
  client_location_.GetLocation(got_location);
  ASSERT_EQ(got_location.ToString(), "{region: china, zone: beijing, campus: }");
}

struct ThreadData {
  ClientLocation *client_location_;
  std::mutex begin_;
  int zone_id_;
};

void *UpdateLocation(void *args) {
  ThreadData *data = static_cast<ThreadData *>(args);
  data->begin_.lock();
  int zone_id = ++data->zone_id_;
  data->begin_.unlock();
  Location location;
  location.zone = std::to_string(zone_id);
  data->client_location_->Update(location);
  return nullptr;
}

TEST_F(ClientLocationTest, MultiThreadTest) {
  ThreadData data;
  data.client_location_ = &client_location_;
  data.zone_id_ = 0;
  std::vector<pthread_t> thread_list;
  pthread_t tid;
  data.begin_.lock();  // 先上锁，让新创建的所有线程等待
  for (int i = 0; i < 10; ++i) {
    pthread_create(&tid, nullptr, UpdateLocation, &data);
    thread_list.push_back(tid);
  }
  data.begin_.unlock();
  client_location_.WaitInit(1000);
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], nullptr);
  }
  ASSERT_EQ(client_location_.GetVersion(), 10);
}

}  // namespace model
}  // namespace polaris
