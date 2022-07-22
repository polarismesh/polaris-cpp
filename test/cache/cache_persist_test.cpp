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

#include "cache/cache_persist.h"

#include <gtest/gtest.h>

#include <fstream>
#include <streambuf>
#include <string>
#include <vector>

#include "cache/persist_task.h"
#include "mock/fake_server_response.h"
#include "reactor/reactor.h"
#include "test_utils.h"
#include "utils/file_utils.h"

namespace polaris {

static Config *CreateConfig(const std::string &content) {
  std::string err_msg;
  Config *config = Config::CreateFromString(content, err_msg);
  EXPECT_TRUE(config != nullptr && err_msg.empty());
  return config;
}

TEST(CachePersistConfigTest, TestInitDefault) {
  Config *config = CreateConfig("");
  ASSERT_TRUE(config != nullptr);
  CachePersistConfig persist_config;
  ASSERT_TRUE(persist_config.Init(config));
  delete config;
  ASSERT_TRUE(!persist_config.GetPersistDir().empty());
  ASSERT_EQ(persist_config.GetMaxWriteRetry(), 5);
  ASSERT_EQ(persist_config.GetRetryInterval(), 1000);
}

TEST(CachePersistConfigTest, TestErrorMaxWriteRetry) {
  Config *config = CreateConfig("persistMaxWriteRetry: -1");
  ASSERT_TRUE(config != nullptr);
  CachePersistConfig persist_config;
  ASSERT_FALSE(persist_config.Init(config));
  delete config;
}

TEST(CachePersistConfigTest, TestErrorRetryInterval) {
  Config *config = CreateConfig("persistRetryInterval: 0");
  ASSERT_TRUE(config != nullptr);
  CachePersistConfig persist_config;
  ASSERT_FALSE(persist_config.Init(config));
  delete config;
}

class CachePersistTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    cache_persist = new CachePersist(reactor_);
    ASSERT_TRUE(TestUtils::CreateTempDir(persist_dir_));
    std::string content = "persistDir: " + persist_dir_;
    Config *config = CreateConfig(content);
    ASSERT_TRUE(config != nullptr);
    ASSERT_EQ(cache_persist->Init(config), kReturnOk);
    delete config;
  }

  virtual void TearDown() {
    if (cache_persist != nullptr) {
      delete cache_persist;
      cache_persist = nullptr;
    }
    reactor_.Stop();
    if (!persist_dir_.empty()) {
      TestUtils::RemoveDir(persist_dir_);
    }
  }

 protected:
  Reactor reactor_;
  std::string persist_dir_;
  CachePersist *cache_persist;
};

TEST_F(CachePersistTest, LoadFromNonexistDir) {
  // 从不存在的文件夹加载，只是创建文件夹后返回
  std::string persist_dir = "/tmp/polaris_test_no_exist_dir";
  if (FileUtils::FileExists(persist_dir)) {
    ASSERT_TRUE(TestUtils::RemoveDir(persist_dir));
  }
  std::string content = "persistDir: " + persist_dir;
  Config *config = CreateConfig(content);
  ASSERT_TRUE(config != nullptr);
  ASSERT_EQ(cache_persist->Init(config), kReturnOk);
  delete config;

  std::unique_ptr<Location> location = cache_persist->LoadLocation();
  ASSERT_TRUE(location == nullptr);

  ASSERT_TRUE(FileUtils::FileExists(persist_dir));
  TestUtils::RemoveDir(persist_dir);
}

TEST_F(CachePersistTest, PersistAndDeleteServiceData) {
  ServiceKey service_key = {"test", "test.cache"};
  for (int i = 0; i < 10; ++i) {
    ServiceData *service_data = nullptr;
    if (i % 3 == 0) {
      v1::DiscoverResponse response;
      FakeServer::CreateServiceInstances(response, service_key, 10 + i);
      service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
    }
    std::string data = service_data != nullptr ? service_data->ToJsonString() : "";
    cache_persist->PersistServiceData(service_key, kServiceDataInstances, data);
    reactor_.RunOnce();

    std::unique_ptr<Location> load_location = cache_persist->LoadLocation();
    ServiceData *disk_service_data = cache_persist->LoadServiceData(service_key, kServiceDataInstances);
    ASSERT_TRUE(load_location == nullptr);
    if (service_data != nullptr) {
      ASSERT_TRUE(disk_service_data != nullptr);
      service_data->DecrementRef();
      disk_service_data->DecrementRef();
    }
  }
}

TEST_F(CachePersistTest, PersistAndLoadLocation) {
  for (int i = 0; i < 10; ++i) {
    Location persist_location = {"华南", "深圳", "大学城" + std::to_string(i)};
    cache_persist->PersistLocation(persist_location);
    reactor_.RunOnce();

    std::unique_ptr<Location> load_location = cache_persist->LoadLocation();
    ASSERT_TRUE(load_location != nullptr);
    ASSERT_EQ(persist_location.region, load_location->region);
    ASSERT_EQ(persist_location.zone, load_location->zone);
    ASSERT_EQ(persist_location.campus, load_location->campus);
  }
}

TEST_F(CachePersistTest, PersistAndLoad) {
  int count = 10;
  ServiceData *disk_service_data;
  for (int i = 1; i <= count; ++i) {
    ServiceKey service_key = {"test", "test.cache" + std::to_string(i)};
    v1::DiscoverResponse response;
    FakeServer::CreateServiceInstances(response, service_key, i);
    ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
    cache_persist->PersistServiceData(service_key, kServiceDataInstances, service_data->ToJsonString());
    Location persist_location = {"华南", "深圳", "大学城" + std::to_string(i)};
    cache_persist->PersistLocation(persist_location);
    reactor_.RunOnce();
    service_data->DecrementRef();
    disk_service_data = cache_persist->LoadServiceData(service_key, kServiceDataInstances);
    ASSERT_TRUE(disk_service_data != nullptr);
    disk_service_data->DecrementRef();
  }
  std::unique_ptr<Location> load_location = cache_persist->LoadLocation();
  ASSERT_TRUE(load_location != nullptr);
}

struct ThreadArg {
  pthread_t tid;
  std::string file;
  char ch;
};

static const int kDataSize = 20000;

void *PersistThreadFunc(void *arg) {
  Reactor reactor;
  ThreadArg *func_arg = static_cast<ThreadArg *>(arg);
  std::string data(kDataSize, func_arg->ch);
  for (int i = 0; i < 3000; ++i) {
    reactor.SubmitTask(new PersistTask(func_arg->file, data, 1, 1));
  }
  reactor.RunOnce();
  reactor.Stop();
  return nullptr;
}

class MultiThreadPersistTest : public ::testing::Test {
 protected:
  virtual void SetUp() { ASSERT_TRUE(TestUtils::CreateTempDir(persist_dir_)); }

  virtual void TearDown() {
    if (!persist_dir_.empty()) {
      TestUtils::RemoveDir(persist_dir_);
    }
  }

 protected:
  std::string persist_dir_;
};

// 多线程持久化校验
TEST_F(MultiThreadPersistTest, TestDoPersist) {
  int thread_size = 4;
  std::string file = persist_dir_ + "/polaris_data.bin";
  ThreadArg thread_arg[thread_size];
  for (int i = 0; i < thread_size; ++i) {
    thread_arg[i].file = file;
    thread_arg[i].ch = 'A' + i;
    int rc = pthread_create(&thread_arg[i].tid, nullptr, PersistThreadFunc, &thread_arg[i]);
    ASSERT_EQ(rc, 0);
  }
  for (int i = 0; i < thread_size; ++i) {
    int rc = pthread_join(thread_arg[i].tid, nullptr);
    ASSERT_EQ(rc, 0);
  }
  std::ifstream input_file(file.c_str());
  ASSERT_TRUE(!input_file.bad());
  std::string data((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());
  input_file.close();
  ASSERT_EQ(data.size(), kDataSize);
  for (std::size_t i = 1; i < data.size(); ++i) {
    ASSERT_EQ(data[0], data[i]) << i;
  }
}

}  // namespace polaris
