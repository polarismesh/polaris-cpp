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

#ifndef POLARIS_CPP_POLARIS_MODEL_INSTANCE_H_
#define POLARIS_CPP_POLARIS_MODEL_INSTANCE_H_

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "polaris/instance.h"

namespace v1 {
class Instance;
}

namespace polaris {

/// 实例远程数据，从服务端下发
struct InstanceRemoteValue {
  InstanceRemoteValue();
  InstanceRemoteValue(const std::string& id, const std::string& host, const int& port, const uint32_t& weight);

  void InitFromPb(const v1::Instance& instance);

  // 实例基本字段
  std::string id_;
  std::string host_;
  int port_;
  uint32_t weight_;
  std::string vpc_id_;

  int priority_;
  bool is_ipv6_;
  bool is_healthy_;
  bool is_isolate_;

  // 实例元数据标签
  std::map<std::string, std::string> metadata_;
  // 以下字段实际在metadata中也会存储，取出来用于加速获取
  std::string protocol_;           // 实例协议
  std::string version_;            // 实例版本
  std::string container_name_;     // 容器名
  std::string internal_set_name_;  // 三段式Set名

  // 实例位置信息
  std::string region_;
  std::string zone_;
  std::string campus_;

  // 逻辑set，暂未使用
  std::string logic_set_;
};

/// 实例本地数据，SDK生成的数据
struct InstanceLocalValue {
  InstanceLocalValue() : local_id_(0), dynamic_weight_(0), hash_(0) {}

  // AcquireXXX/ReleaseXXX 必须配对使用
  std::vector<uint64_t>& AcquireVnodeHash() {
    vnode_hash_mutex_.lock();
    return vnode_hash_;
  }
  void ReleaseVnodeHash() { vnode_hash_mutex_.unlock(); }

  // 本地生成的id，用于返回给tRPC做连接池索引
  uint64_t local_id_;

  // 动态权重，由本地动态调整策略，或者动态权重服务下发调整
  uint32_t dynamic_weight_;

  // 一致性hash负载均衡缓存的hash值
  uint64_t hash_;
  std::vector<uint64_t> vnode_hash_;
  std::mutex vnode_hash_mutex_;
};

/// 实例独享数据，复制时需要单独拷贝
struct InstanceOwnedValue {
  InstanceOwnedValue() : locality_aware_info_(0) {}

  // LA负载均衡跟踪信息，默认值为0，启用LA后为非0值
  uint64_t locality_aware_info_;
};

class InstanceImpl {
 public:
  InstanceImpl();

  InstanceImpl(const std::string& id, const std::string& host, const int& port, const uint32_t& weight);

  ~InstanceImpl() {}

  void InitFromPb(const v1::Instance& instance);

  void SetDynamicWeight(uint32_t dynamic_weight);

  void SetHashValue(uint64_t hashVal);

  void SetLocalId(uint64_t local_id);

  void SetLocalValue(InstanceLocalValue* localValue);

  void CopyLocalValue(const InstanceImpl& impl);

  std::shared_ptr<InstanceLocalValue>& GetLocalValue() { return local_value_; }

  Instance* DumpWithLocalityAwareInfo(uint64_t locality_aware_info);

 private:
  friend class Instance;
  // 远程下发数据
  std::shared_ptr<InstanceRemoteValue> remote_value_;

  // SDK本地数据
  std::shared_ptr<InstanceLocalValue> local_value_;

  // 实例对象独享数据
  InstanceOwnedValue owned_value_;
};

}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_MODEL_INSTANCE_H_
