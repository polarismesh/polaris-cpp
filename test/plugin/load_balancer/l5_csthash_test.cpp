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

#include "plugin/load_balancer/ringhash/l5_csthash.h"

#include <gtest/gtest.h>
#include <stdlib.h>

#include <map>
#include <string>

#include "mock/fake_server_response.h"
#include "model/model_impl.h"
#include "plugin/load_balancer/hash/murmur.h"
#include "test_context.h"
#include "utils/ip_utils.h"
#include "utils/utils.h"

namespace polaris {

namespace l5 {

struct ROUTE_NODE {
  char ip_str[32];     /*ip地址*/
  unsigned int ip;     /*ip地址*/
  unsigned short port; /*端口*/
  int weight;          /*权重*/
};

static inline bool server_comp(const ROUTE_NODE &a, const ROUTE_NODE &b) {
  if (a.weight > b.weight) return true;

  if (a.weight == b.weight) {
    return ((a.ip < b.ip) ? true : ((a.ip == b.ip) ? (a.port < b.port) : false));
  }
  return false;
}

class Cl5CSTHashLB {
 public:
  explicit Cl5CSTHashLB(bool is_brpc_murmurhash) : is_brpc_murmurhash_(is_brpc_murmurhash) {}
  int AddRoute(unsigned int ip, unsigned short port, int weight) {
    ROUTE_NODE node = {"", ip, port, weight};
    snprintf(node.ip_str, sizeof(node.ip_str), "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF,
             (ip >> 24) & 0xFF);
    node.ip_str[sizeof(node.ip_str) - 1] = 0;

    servers.push_back(node);
    return 0;
  }

  int Rebuild() {
    char node[256] = {0};
    unsigned int len;
    unsigned int index;
    unsigned int virtual_node_cnt;
    unsigned int hash;

    CstIterServer server_beg, server_end;
    IterRoute pos;
    for (server_beg = servers.begin(), server_end = servers.end(); server_beg != server_end; server_beg++) {
      /*计算该节点应该建立的虚拟节点数*/
      for (index = 0, virtual_node_cnt = server_beg->weight; index < virtual_node_cnt; index++) {
        /*因为空间足够大故这里不使用snprintf*/
        if (is_brpc_murmurhash_) {
          len = snprintf(node, sizeof(node) - 1, "%s:%u-%u", server_beg->ip_str, server_beg->port, index);
          hash = Murmur3_32(node, len, 0);
        } else {
          len = snprintf(node, sizeof(node) - 1, "%s:%u:%u", server_beg->ip_str, index, server_beg->port);
          hash = Murmur3_32(node, len, 16);
        }
        /*添加到map表中*/
        pos = rb.find(hash);
        if (rb.end() != pos) {
          /*hash冲突后选用weight大的*/
          if (!server_comp(pos->second, *server_beg)) {
            memcpy(&(pos->second), &(*server_beg), sizeof(ROUTE_NODE));
          }
          continue;
        }

        /*不判断是否成功插入???*/
        rb.insert(ElemRoute(hash, *server_beg));
      }
    }
    return 0;
  }

  int GetRoute(unsigned long long key, std::string &ip, unsigned short &port) {
    unsigned int hash = 0;
    ROUTE_NODE *route = nullptr;
    /*寻找hash key >= key的结点*/
    if (is_brpc_murmurhash_) {
      hash = key;
    } else {
      hash = Murmur3_32((const char *)&key, sizeof(key), 16);
    }
    IterRoute pos = rb.lower_bound(hash);
    if (rb.end() != pos) {
      route = &(pos->second);
    } else if (rb.size()) {
      route = &(rb.begin()->second);
    } else {
      return (-1);
    }

    ip = route->ip_str;
    port = route->port;
    return 0;
  }

 private:
  typedef std::vector<ROUTE_NODE> Servers;
  typedef Servers::iterator IterServer;
  typedef Servers::const_iterator CstIterServer;
  Servers servers; /*用于存储正常的server也即weight > 0的server*/

  typedef std::map<unsigned int, ROUTE_NODE> RouteTable;
  typedef RouteTable::iterator IterRoute;
  typedef RouteTable::value_type ElemRoute;
  typedef std::pair<IterRoute, bool> RetRoute;

  /*模拟hash环*/
  RouteTable rb;
  bool is_brpc_murmurhash_;
};

}  // namespace l5

class L5CsthashLbTest : public ::testing::Test {
  virtual void SetUp() {
    context_ = TestContext::CreateContext();
    ASSERT_TRUE(context_ != nullptr);
    l5_csthash_lb_ = new L5CstHashLoadBalancer();
    ASSERT_EQ(l5_csthash_lb_->Init(nullptr, context_), kReturnOk);
    brpc_murmurhash_lb_ = new L5CstHashLoadBalancer(true);
    ASSERT_EQ(brpc_murmurhash_lb_->Init(nullptr, context_), kReturnOk);
    srandom(time(nullptr));
  }

  virtual void TearDown() {
    if (l5_csthash_lb_ != nullptr) {
      delete l5_csthash_lb_;
      l5_csthash_lb_ = nullptr;
    }
    if (brpc_murmurhash_lb_ != nullptr) {
      delete brpc_murmurhash_lb_;
      brpc_murmurhash_lb_ = nullptr;
    }
    if (context_ != nullptr) {
      delete context_;
      context_ = nullptr;
    }
  }

 protected:
  L5CstHashLoadBalancer *l5_csthash_lb_;
  L5CstHashLoadBalancer *brpc_murmurhash_lb_;
  Context *context_;
};

TEST_F(L5CsthashLbTest, TestSelectInstance) {
  l5::Cl5CSTHashLB old_l5_cst_lb(false);
  l5::Cl5CSTHashLB brpc_hash_lb(true);
  ServiceKey service_key = {"test_namespace", "test_name"};
  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key);
  for (int i = 0; i < 40 + random() % 20; ++i) {
    std::string host = std::to_string(random() % 255) + "." + std::to_string(random() % 255) + "." +
                       std::to_string(random() % 255) + "." + std::to_string(random() % 255);
    uint32_t ip;
    ASSERT_TRUE(IpUtils::StrIpToInt(host, ip));
    int port = 8000 + i;
    int weight = 80 + random() % 40;

    old_l5_cst_lb.AddRoute(ip, port, weight);
    brpc_hash_lb.AddRoute(ip, port, weight);

    v1::Instance *instance = response.add_instances();
    instance->mutable_id()->set_value("instance_" + std::to_string(i));
    instance->mutable_host()->set_value(host);
    instance->mutable_port()->set_value(port);
    instance->mutable_weight()->set_value(weight);
  }
  old_l5_cst_lb.Rebuild();
  brpc_hash_lb.Rebuild();
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  Service service(service_key, 1);
  service.UpdateData(service_data);
  ServiceInstances service_instances(service_data);
  for (int i = 0; i < 10000; ++i) {
    Instance *instance = nullptr;
    Criteria criteria;
    criteria.hash_key_ = random();
    ReturnCode ret_code = l5_csthash_lb_->ChooseInstance(&service_instances, criteria, instance);
    ASSERT_EQ(ret_code, kReturnOk);

    std::string ip;
    unsigned short port;
    int rc = old_l5_cst_lb.GetRoute(criteria.hash_key_, ip, port);
    ASSERT_EQ(rc, 0);

    ASSERT_EQ(ip, instance->GetHost()) << i;
    ASSERT_EQ(port, instance->GetPort());
  }

  for (int i = 0; i < 10000; ++i) {
    Instance *instance = nullptr;
    Criteria criteria;
    criteria.hash_key_ = random();
    ReturnCode ret_code = brpc_murmurhash_lb_->ChooseInstance(&service_instances, criteria, instance);
    ASSERT_EQ(ret_code, kReturnOk);

    std::string ip;
    unsigned short port;
    int rc = brpc_hash_lb.GetRoute(criteria.hash_key_, ip, port);
    ASSERT_EQ(rc, 0);

    ASSERT_EQ(ip, instance->GetHost()) << i;
    ASSERT_EQ(port, instance->GetPort());
  }
  service_data->DecrementRef();
}

TEST_F(L5CsthashLbTest, TestSelectReplicateInstance) {
  ServiceKey service_key = {"test_namespace", "test_name"};
  v1::DiscoverResponse response;
  FakeServer::InstancesResponse(response, service_key);
  int instance_count = 10;
  for (int i = 0; i < instance_count; ++i) {
    v1::Instance *instance = response.add_instances();
    instance->mutable_id()->set_value("instance_" + std::to_string(i));
    instance->mutable_host()->set_value("host" + std::to_string(random()));
    instance->mutable_port()->set_value(8081 + i);
    instance->mutable_weight()->set_value(80 + random() % 40);
  }
  ServiceData *service_data = ServiceData::CreateFromPb(&response, kDataIsSyncing);
  Service service(service_key, 1);
  service.UpdateData(service_data);
  ServiceInstances service_instances(service_data);
  for (int i = 0; i < 1000; ++i) {
    Criteria criteria;
    criteria.hash_key_ = random();
    std::set<Instance *> l5_instance_set;
    for (int j = 0; j < instance_count + 1; ++j) {
      Instance *instance = nullptr, *instance1 = nullptr;
      criteria.replicate_index_ = j;
      ASSERT_EQ(l5_csthash_lb_->ChooseInstance(&service_instances, criteria, instance), kReturnOk);
      ASSERT_EQ(l5_csthash_lb_->ChooseInstance(&service_instances, criteria, instance1), kReturnOk);
      ASSERT_TRUE(instance != nullptr);
      ASSERT_EQ(instance, instance1);
      if (j < instance_count) {
        ASSERT_TRUE(l5_instance_set.insert(instance).second);  // 验证去重
      } else {
        instance1 = nullptr;
        criteria.replicate_index_ = 0;
        ASSERT_EQ(l5_csthash_lb_->ChooseInstance(&service_instances, criteria, instance1), kReturnOk);
        ASSERT_TRUE(instance1 != nullptr);
        ASSERT_EQ(instance, instance1);
      }
    }
    std::set<Instance *> brpc_instance_set;
    for (int j = 0; j < instance_count + 1; ++j) {
      Instance *instance, *instance1 = nullptr;
      criteria.replicate_index_ = j;
      ASSERT_EQ(brpc_murmurhash_lb_->ChooseInstance(&service_instances, criteria, instance), kReturnOk);
      ASSERT_EQ(brpc_murmurhash_lb_->ChooseInstance(&service_instances, criteria, instance1), kReturnOk);
      ASSERT_TRUE(instance != nullptr);
      ASSERT_EQ(instance, instance1);
      if (j < instance_count) {
        ASSERT_TRUE(brpc_instance_set.insert(instance).second);  // 验证去重
      } else {
        instance1 = nullptr;
        criteria.replicate_index_ = 0;
        ASSERT_EQ(brpc_murmurhash_lb_->ChooseInstance(&service_instances, criteria, instance1), kReturnOk);
        ASSERT_TRUE(instance1 != nullptr);
        ASSERT_EQ(instance, instance1);
      }
    }
  }
  service_data->DecrementRef();
}

}  // namespace polaris
