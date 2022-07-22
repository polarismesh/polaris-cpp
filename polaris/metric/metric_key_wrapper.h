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

#ifndef POLARIS_CPP_POLARIS_METRIC_METRIC_KEY_WRAPPER_H_
#define POLARIS_CPP_POLARIS_METRIC_METRIC_KEY_WRAPPER_H_

namespace v1 {
class MetricKey;
}

namespace polaris {

// 封装MetricKey用于索引
class MetricKeyWrapper {
 public:
  MetricKeyWrapper();

  explicit MetricKeyWrapper(v1::MetricKey* metric_key);

  explicit MetricKeyWrapper(const v1::MetricKey& metric_key);

  MetricKeyWrapper(const MetricKeyWrapper& other);

  ~MetricKeyWrapper();

  bool operator<(const MetricKeyWrapper& other) const;

  const MetricKeyWrapper& operator=(const MetricKeyWrapper& other);

 private:
  bool owned_;
  v1::MetricKey* metric_key_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_METRIC_METRIC_KEY_WRAPPER_H_
