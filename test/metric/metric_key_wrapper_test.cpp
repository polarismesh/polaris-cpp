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

#include "metric/metric_key_wrapper.h"

#include <gtest/gtest.h>

#include <set>

#include "utils/scoped_ptr.h"
#include "v1/metric.pb.h"

namespace polaris {

TEST(MetricKeyWrapperTest, MetricKeySet) {
  std::set<MetricKeyWrapper> metric_key_set;
  ScopedPtr<v1::MetricKey> metric_key(new v1::MetricKey());
  MetricKeyWrapper owned_key(*metric_key);
  MetricKeyWrapper ref_key(metric_key.Get());
  metric_key_set.insert(ref_key);
  ASSERT_EQ(metric_key_set.count(owned_key), 1);
  ASSERT_EQ(metric_key_set.count(ref_key), 1);

  metric_key->set_role(v1::MetricKey::Callee);
  ASSERT_EQ(metric_key_set.count(ref_key), 0);
  metric_key_set.insert(ref_key);
  ASSERT_EQ(metric_key_set.size(), 2);
}

}  // namespace polaris
