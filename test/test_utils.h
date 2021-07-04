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

#ifndef POLARIS_CPP_TEST_TEST_UTILS_H_
#define POLARIS_CPP_TEST_TEST_UTILS_H_

#include <ftw.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "utils/string_utils.h"
#include "utils/time_clock.h"
#include "utils/utils.h"

namespace polaris {

extern void (*current_time_impl)(timespec &ts);
extern void (*current_time_impl_backup)(timespec &ts);
extern volatile uint64_t g_fake_time_now_ms;

class TestUtils {
public:
  static void SetUpFakeTime() {
    g_fake_time_now_ms       = Time::GetCurrentTimeMs();
    current_time_impl_backup = current_time_impl;
    current_time_impl        = FakeNow;
  }

  static void TearDownFakeTime() { current_time_impl = current_time_impl_backup; }

  static void FakeNowIncrement(uint64_t add_ms) { ATOMIC_ADD(&g_fake_time_now_ms, add_ms); }

private:
  static void FakeNow(timespec &ts) {
    ts.tv_sec  = g_fake_time_now_ms / Time::kThousandBase;
    ts.tv_nsec = (g_fake_time_now_ms % Time::kThousandBase) * Time::kMillionBase;
  }

public:
  static int PickUnusedPort() {
    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_port        = 0;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      return -1;
    }

    if (bind(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
      close(sock);
      return -1;
    }

    socklen_t addr_len = sizeof(addr);
    if (getsockname(sock, reinterpret_cast<struct sockaddr *>(&addr), &addr_len) != 0 ||
        addr_len > sizeof(addr)) {
      close(sock);
      return -1;
    }
    close(sock);
    return ntohs(addr.sin_port);
  }

  static bool CreateTempFile(std::string &file) {
    char temp_file[] = "/tmp/polaris_test_XXXXXX";
    int fd           = mkstemp(temp_file);
    if (fd < 0) return false;
    close(fd);
    file = temp_file;
    return true;
  }

  static bool CreateTempFileWithContent(std::string &file, const std::string &content) {
    char temp_file[] = "/tmp/polaris_test_XXXXXX";
    int fd           = mkstemp(temp_file);
    if (fd < 0) return false;
    std::size_t len = write(fd, content.c_str(), content.size());
    if (len != content.size()) return false;
    close(fd);
    file = temp_file;
    return true;
  }

  static bool CreateTempDir(std::string &dir) {
    char temp_dir[] = "/tmp/polaris_test_XXXXXX";
    char *dir_name  = mkdtemp(temp_dir);
    if (dir_name == NULL) return false;
    dir = temp_dir;
    return true;
  }

  static bool RemoveDir(const std::string &dir) {
    return nftw(dir.c_str(), RemoveFile, 10, FTW_DEPTH | FTW_PHYS) == 0;
  }

private:
  static int RemoveFile(const char *path, const struct stat * /*sbuf*/, int /*type*/,
                        struct FTW * /*ftwb*/) {
    return remove(path);
  }
};

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_TEST_UTILS_H_
