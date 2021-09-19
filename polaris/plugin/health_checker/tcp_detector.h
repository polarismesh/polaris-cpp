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

#ifndef POLARIS_CPP_POLARIS_PLUGIN_OUTLIER_DETECTOR_TCP_DETECTOR_H_
#define POLARIS_CPP_POLARIS_PLUGIN_OUTLIER_DETECTOR_TCP_DETECTOR_H_

#include <stdint.h>

#include <string>

#include "polaris/defs.h"
#include "polaris/plugin.h"

namespace polaris {

class Config;
class Context;
class Instance;

class TcpHealthChecker : public HealthChecker {
public:
  TcpHealthChecker();

  virtual ~TcpHealthChecker();

  virtual ReturnCode Init(Config* config, Context* context);

  virtual ReturnCode DetectInstance(Instance& instance, DetectResult& detect_result);

private:
  std::string send_package_;
  std::string receive_package_;
  uint64_t timeout_ms_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_PLUGIN_OUTLIER_DETECTOR_TCP_DETECTOR_H_
