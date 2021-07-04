//  Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//  https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed
//  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//  CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
//  language governing permissions and limitations under the License.
//

#include "utils/file_utils.h"

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include <sstream>
#include <string>

namespace polaris {

std::string FileUtils::ExpandPath(const std::string& path) {
  std::size_t begin_index = path.find("$");
  if (begin_index == std::string::npos) {
    return path;
  }

  std::string before = path.substr(0, begin_index);
  std::string after  = path.substr(begin_index + 1);
  std::string variable;
  std::size_t end_index = after.find('/');
  if (end_index == std::string::npos) {
    variable.swap(after);
  } else {
    variable = after.substr(0, end_index);
    after    = after.substr(end_index);
  }

  std::string value;
  char* v = NULL;
  if (variable.compare("HOME") == 0) {
    // HOME环境变量需要从用户属性获取，避免程序调用setuid导致$HOME还指向之前用户的问题
    v = getpwuid(getuid())->pw_dir;
  } else {
    v = getenv(variable.c_str());
  }
  if (v != NULL) {
    value = std::string(v);
  }

  return ExpandPath(before + value + after);
}

bool FileUtils::CreatePath(const std::string& path) {
  struct stat statbuf;
  if (stat(path.c_str(), &statbuf) == 0) {
    return S_ISDIR(statbuf.st_mode) != 0 ? true : false;
  }

  if (mkdir(path.c_str(), 0775) == 0) {
    return true;
  }

  std::size_t index = path.find('/');
  while (index != std::string::npos) {
    if (mkdir(path.substr(0, index + 1).c_str(), 0775) != 0 && EEXIST != errno) {
      return false;
    }
    index = path.find('/', index + 1);
  }
  if (mkdir(path.c_str(), 0775) == 0) {
    return true;
  }
  return true;
}

bool FileUtils::FileExists(const std::string& file) {
  struct stat st;
  return stat(file.c_str(), &st) == 0;
}

bool FileUtils::RegFileExists(const std::string& file) {
  struct stat st;
  return stat(file.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool FileUtils::UpdateModifiedTime(const std::string& file) {
  struct stat st;
  if (stat(file.c_str(), &st) != 0) {
    return false;
  }
  struct utimbuf new_times;
  new_times.actime  = st.st_atime;  // 访问时间不变
  new_times.modtime = time(NULL);   // 修改时间为当前时间
  return utime(file.c_str(), &new_times) == 0;
}

bool FileUtils::GetModifiedTime(const std::string& file, uint64_t* modified_time) {
  struct stat st;
  if (stat(file.c_str(), &st) == 0) {
    *modified_time = st.st_mtime * 1000;
    return true;
  }
  return false;
}

bool FileUtils::RemoveFile(const std::string& file) { return remove(file.c_str()) == 0; }

}  // namespace polaris
