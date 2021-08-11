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

#ifndef POLARIS_CPP_POLARIS_UTILS_FILE_UTILS_H_
#define POLARIS_CPP_POLARIS_UTILS_FILE_UTILS_H_

#include <stdint.h>

#include <string>

namespace polaris {

class FileUtils {
public:
  // 展开文件路径中包含的环境变量
  static std::string ExpandPath(const std::string& path);

  // 创建目录
  static bool CreatePath(const std::string& path);

  // 文件是否存在
  static bool FileExists(const std::string& file);

  // 文件是否存在且是否常规文件
  static bool RegFileExists(const std::string& file);

  // 更新文件的最后修改时间
  static bool UpdateModifiedTime(const std::string& file);

  // 获取文件的最后修改时间
  static bool GetModifiedTime(const std::string& file, uint64_t* modified_time);

  // 删除文件
  static bool RemoveFile(const std::string& file);
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_UTILS_FILE_UTILS_H_
