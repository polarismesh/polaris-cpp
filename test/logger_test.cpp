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

#include <string>
#include <vector>

#include "logger.h"
#include "test_utils.h"
#include "utils/file_utils.h"

namespace polaris {

class LoggerTest : public ::testing::Test {
protected:
  virtual void SetUp() {
    TestUtils::CreateTempDir(log_path_);
    log_file_name_ = "test.log";
    max_file_size_ = 100;
    max_file_no_   = 2;
  }

  virtual void TearDown() { TestUtils::RemoveDir(log_path_); }

protected:
  std::string log_path_;
  std::string log_file_name_;
  int max_file_size_;
  int max_file_no_;
};

TEST_F(LoggerTest, TestLogLevel) {
  Logger *logger = new LoggerImpl(log_path_, log_file_name_, max_file_size_, max_file_no_);

  logger->SetLogLevel(kTraceLogLevel);
  ASSERT_TRUE(logger->isLevelEnabled(kTraceLogLevel));
  ASSERT_TRUE(logger->isLevelEnabled(kDebugLogLevel));

  logger->SetLogLevel(kDebugLogLevel);
  ASSERT_FALSE(logger->isLevelEnabled(kTraceLogLevel));
  ASSERT_TRUE(logger->isLevelEnabled(kDebugLogLevel));
  ASSERT_TRUE(logger->isLevelEnabled(kInfoLogLevel));

  logger->SetLogLevel(kFatalLogLevel);
  ASSERT_FALSE(logger->isLevelEnabled(kWarnLogLevel));
  ASSERT_FALSE(logger->isLevelEnabled(kErrorLogLevel));
  ASSERT_TRUE(logger->isLevelEnabled(kFatalLogLevel));

  delete logger;
  logger = NULL;
}

TEST_F(LoggerTest, TestFileShift) {
  LoggerImpl *logger = new LoggerImpl(log_path_, log_file_name_, max_file_size_, max_file_no_);
  logger->SetLogLevel(kTraceLogLevel);

  const char *text = "test logger file shift";
  logger->Log(LOG_INFO, text);
  ASSERT_TRUE(FileUtils::FileExists(log_path_ + "/" + log_file_name_));
  ASSERT_TRUE(logger->log_file_ != NULL);
  ASSERT_TRUE(logger->cur_file_size_ > 0);

  int text_log_size = logger->cur_file_size_;
  int log_count     = max_file_size_ / text_log_size + 1;
  for (int i = 0; i < log_count; ++i) {
    logger->Log(LOG_INFO, text);
  }
  ASSERT_TRUE(logger->log_file_ != NULL);
  ASSERT_TRUE(logger->cur_file_size_ <= max_file_size_ + text_log_size);
  ASSERT_TRUE(FileUtils::FileExists(log_path_ + "/" + log_file_name_ + ".0"));

  log_count = log_count * (max_file_no_ + 1);  // 足够序号重复滚动
  for (int i = 0; i < log_count; ++i) {
    logger->Log(LOG_INFO, text);
  }
  ASSERT_TRUE(logger->log_file_ != NULL);
  ASSERT_TRUE(logger->cur_file_size_ <= max_file_size_ + text_log_size);
  // 三个文件都存在
  ASSERT_TRUE(FileUtils::FileExists(log_path_ + "/" + log_file_name_));
  ASSERT_TRUE(FileUtils::FileExists(log_path_ + "/" + log_file_name_ + ".0"));
  ASSERT_TRUE(FileUtils::FileExists(log_path_ + "/" + log_file_name_ + ".1"));

  delete logger;
  logger = NULL;
}

struct WriteLogParam {
  Logger *logger;
  bool stop;
};

void *WriteLogThread(void *args) {
  WriteLogParam *param = static_cast<WriteLogParam *>(args);
  Logger *logger       = param->logger;
  while (!param->stop) {
    logger->Log(LOG_INFO, "check multi thread write log is safe");
  }
  return NULL;
}

TEST_F(LoggerTest, MultiThreadWriteLog) {
  LoggerImpl *logger = new LoggerImpl(log_path_, log_file_name_, max_file_size_, max_file_no_);
  logger->SetLogLevel(kTraceLogLevel);
  WriteLogParam param;
  param.logger = logger;
  param.stop   = false;
  std::vector<pthread_t> thread_list;
  pthread_t tid;
  for (int i = 0; i < 6; ++i) {
    pthread_create(&tid, NULL, WriteLogThread, &param);
    ASSERT_TRUE(tid > 0);
    thread_list.push_back(tid);
  }
  sleep(1);
  param.stop = true;
  for (std::size_t i = 0; i < thread_list.size(); ++i) {
    pthread_join(thread_list[i], NULL);
  }
  delete logger;
}

TEST_F(LoggerTest, TestChangeLogDir) {
  LoggerImpl *logger = new LoggerImpl(log_path_, log_file_name_, max_file_size_, max_file_no_);
  ASSERT_TRUE(logger != NULL);
  ASSERT_FALSE(FileUtils::FileExists(log_path_ + "/" + log_file_name_));

  std::string new_log_dir;
  TestUtils::CreateTempDir(new_log_dir);
  logger->SetLogDir(new_log_dir);
  ASSERT_TRUE(FileUtils::FileExists(new_log_dir + "/" + log_file_name_));
  logger->Log(LOG_INFO, "test change log");
  ASSERT_FALSE(FileUtils::FileExists(log_path_ + "/" + log_file_name_));

  delete logger;
  logger = NULL;
  TestUtils::RemoveDir(new_log_dir);
}

TEST_F(LoggerTest, TestChangeLog) {
  LoggerImpl *logger = new LoggerImpl(log_path_, log_file_name_, max_file_size_, max_file_no_);
  ASSERT_TRUE(logger != NULL);

  Logger *polaris_log = GetLogger();
  ASSERT_NE(polaris_log, logger);
  SetLogger(logger);
  Logger *get_log = GetLogger();
  ASSERT_EQ(get_log, logger);
  SetLogger(NULL);
  get_log = GetLogger();
  ASSERT_EQ(get_log, polaris_log);

  polaris_log = GetStatLogger();
  ASSERT_NE(polaris_log, logger);
  SetStatLogger(logger);
  get_log = GetStatLogger();
  ASSERT_EQ(get_log, logger);
  SetStatLogger(NULL);
  get_log = GetStatLogger();
  ASSERT_EQ(get_log, polaris_log);

  delete logger;
  logger = NULL;
}

}  // namespace polaris
