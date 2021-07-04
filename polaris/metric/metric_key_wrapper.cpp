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

#include "metric/metric_key_wrapper.h"

#include <stddef.h>
#include <v1/metric.pb.h>

#include <string>

namespace polaris {

MetricKeyWrapper::MetricKeyWrapper() : owned_(false), metric_key_(NULL) {}

MetricKeyWrapper::MetricKeyWrapper(v1::MetricKey* metric_key)
    : owned_(false), metric_key_(metric_key) {}

MetricKeyWrapper::MetricKeyWrapper(const v1::MetricKey& metric_key) : owned_(true) {
  metric_key_ = new v1::MetricKey();
  metric_key_->CopyFrom(metric_key);
}

MetricKeyWrapper::MetricKeyWrapper(const MetricKeyWrapper& other) : owned_(true) {
  metric_key_ = new v1::MetricKey();
  metric_key_->CopyFrom(*other.metric_key_);
}

MetricKeyWrapper::~MetricKeyWrapper() {
  if (owned_) {
    delete metric_key_;
  }
  owned_      = false;
  metric_key_ = NULL;
}

bool MetricKeyWrapper::operator<(const MetricKeyWrapper& other) const {
  int result = this->metric_key_->namespace_().compare(other.metric_key_->namespace_());
  if (result < 0) {
    return true;
  } else if (result > 0) {
    return false;
  }
  if ((result = this->metric_key_->service().compare(other.metric_key_->service())) < 0) {
    return true;
  } else if (result > 0) {
    return false;
  }
  if ((result = this->metric_key_->subset().compare(other.metric_key_->subset())) < 0) {
    return true;
  } else if (result > 0) {
    return false;
  }
  if ((result = this->metric_key_->labels().compare(other.metric_key_->labels())) < 0) {
    return true;
  } else if (result > 0) {
    return false;
  }
  return this->metric_key_->role() < other.metric_key_->role();
}

const MetricKeyWrapper& MetricKeyWrapper::operator=(const MetricKeyWrapper& other) {
  if (owned_) {
    metric_key_->Clear();
  } else {
    owned_      = true;
    metric_key_ = new v1::MetricKey();
  }
  metric_key_->CopyFrom(*other.metric_key_);
  return *this;
}

}  // namespace polaris
