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

#include <benchmark/benchmark.h>

#include <iostream>
#include <string>

#include "cache/lru_map.h"

namespace polaris {

class BM_LruMap : public benchmark::Fixture {
 public:
  void SetUp(const ::benchmark::State &state) {
    if (state.thread_index == 0) {
      capacity_ = 8096;
      lru_map_.reset(new LruHashMap<int, int>(capacity_, MurmurInt32, LruValueNoOp, LruValueDelete));
    }
  }

  void TearDown(const ::benchmark::State &state) {
    if (state.thread_index == 0) {
      return;
    }
  }

  int capacity_;
  std::unique_ptr<LruHashMap<int, int> > lru_map_;
};

BENCHMARK_DEFINE_F(BM_LruMap, TestUpdate)
(benchmark::State &state) {
  while (state.KeepRunning()) {
    int key = rand() % 4000;
    int op = rand() % 10;
    if (op == 0) {
      int *value = new int(key);
      lru_map_->Update(key, value);
    } else {
      lru_map_->Get(key);
    }
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(BM_LruMap, TestUpdate)
    ->ThreadRange(1, 8)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(1)
    ->UseRealTime();

}  // namespace polaris
