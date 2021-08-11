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

#include "plugin/service_router/nearby_router.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "mock/fake_server_response.h"
#include "model/model_impl.h"
#include "test_context.h"
#include "utils/string_utils.h"

namespace polaris {

static bool InitNearbyRouterConfig(NearbyRouterConfig &nearby_router_config,
                                   const std::string &content) {
  std::string err_msg;
  Config *config = Config::CreateFromString(content, err_msg);
  EXPECT_TRUE(config != NULL && err_msg.empty());
  bool result = nearby_router_config.Init(config);
  delete config;
  return result;
}

// 就近路由配置测试
class NearbyRouterConfigTest : public ::testing::Test {
  virtual void SetUp() {}

  virtual void TearDown() {}

protected:
  NearbyRouterConfig nearby_config_;
  std::string content_;
};

TEST_F(NearbyRouterConfigTest, InitSuccess) {
  content_ = "";
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_config_, content_));

  content_ = "matchLevel: region\nmaxMatchLevel: none";
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_config_, content_));
  content_ = "matchLevel: zone\nmaxMatchLevel: region";
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_config_, content_));
  content_ = "matchLevel: campus\nmaxMatchLevel: zone";
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_config_, content_));
  content_ = "matchLevel: campus\nmaxMatchLevel: campus";
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_config_, content_));

  content_ = "strictNearby: false";
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_config_, content_));
  content_ = "strictNearby: true";
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_config_, content_));

  content_ = "enableDegradeByUnhealthyPercent: false";
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_config_, content_));
  content_ = "enableDegradeByUnhealthyPercent: true";
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_config_, content_));
  content_ = "unhealthyPercentToDegrade: 1";
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_config_, content_));
  content_ = "unhealthyPercentToDegrade: 100";
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_config_, content_));

  content_ = "enableRecoverAll: true";
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_config_, content_));
  content_ = "enableRecoverAll: false";
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_config_, content_));
}

TEST_F(NearbyRouterConfigTest, InitFailed) {
  content_ = "matchLevel: xxx";
  ASSERT_FALSE(InitNearbyRouterConfig(nearby_config_, content_));
  content_ = "maxMatchLevel: xxx";
  ASSERT_FALSE(InitNearbyRouterConfig(nearby_config_, content_));

  content_ = "matchLevel: region\nmaxMatchLevel: campus";
  ASSERT_FALSE(InitNearbyRouterConfig(nearby_config_, content_));

  content_ = "unhealthyPercentToDegrade: 0";
  ASSERT_FALSE(InitNearbyRouterConfig(nearby_config_, content_));
  content_ = "unhealthyPercentToDegrade: 101";
  ASSERT_FALSE(InitNearbyRouterConfig(nearby_config_, content_));
}

// 就近路由cluster测试
class NearbyRouterClusterTest : public ::testing::Test {
protected:
  virtual void SetUp() { CreateInstances(instances_); }
  virtual void TearDown() {
    for (std::size_t i = 0; i < instances_.size(); i++) {
      delete instances_[i];
    }
  }

  static void CreateInstances(std::vector<Instance *> &instances);

  std::vector<Instance *> instances_;
  std::set<Instance *> unhealthy_set_;
  NearbyRouterConfig nearby_router_config_;
  std::vector<Instance *> result_set_;
};

// id      0     1     2    3     4     5     6    7     8     9
// region  华南  华南  华南  华南  华南  华南  华南  华南  华南  华北
// zone    深圳  深圳  深圳  深圳  深圳  深圳  广州  广州  广州  北京
// campus  南山  南山  南山  宝安  宝安  宝安  南山  南山  南山  朝阳
void NearbyRouterClusterTest::CreateInstances(std::vector<Instance *> &instances) {
  for (int i = 0; i < 10; i++) {
    std::string instance_id = "instance_" + StringUtils::TypeToStr<int>(i);
    Instance *instance      = new Instance(instance_id, "host", 8000, 100);
    InstanceSetter setter(*instance);
    if (i < 9) {
      setter.SetRegion("华南");
      setter.SetZone(i < 6 ? "深圳" : "广州");
      setter.SetCampus(i < 3 || i > 5 ? "南山" : "宝安");
    } else {
      setter.SetRegion("华北");
      setter.SetZone("北京");
      setter.SetCampus("朝阳");
    }
    instances.push_back(instance);
  }
}

TEST_F(NearbyRouterClusterTest, DegradeWithDefaultConfig) {
  unhealthy_set_.insert(instances_[0]);
  unhealthy_set_.insert(instances_[1]);
  unhealthy_set_.insert(instances_[2]);

  Location location = {"华南", "深圳", "南山"};
  int match_level;
  for (int i = 0; i < 2; ++i) {
    std::string content = i == 0 ? "" : "matchLevel: campus";
    ASSERT_TRUE(InitNearbyRouterConfig(nearby_router_config_, content));
    NearbyRouterCluster nearby_router_cluster(nearby_router_config_);
    nearby_router_cluster.CalculateSet(location, instances_, unhealthy_set_);
    std::vector<Instance *> result_set;  // 根据健康比例选择实例
    if (i == 0) {
      // case1: 默认匹配region时，还有3个健康节点不用降级
      ASSERT_FALSE(nearby_router_cluster.CalculateResult(result_set, match_level));
    } else {
      // case2: 默认匹配campus时匹配到南山3个节点全不健康降级到zone级别选择深圳其他节点
      ASSERT_TRUE(nearby_router_cluster.CalculateResult(result_set, match_level));
    }
    ASSERT_EQ(result_set.size(), 3);
    ASSERT_EQ(result_set[0], instances_[3]);
    ASSERT_EQ(result_set[1], instances_[4]);
    ASSERT_EQ(result_set[2], instances_[5]);
  }
  {
    // case 3: 限制只匹配campus时，触发全死全活
    std::string content = "matchLevel: campus\nmaxMatchLevel: campus";
    ASSERT_TRUE(InitNearbyRouterConfig(nearby_router_config_, content));
    NearbyRouterCluster nearby_router_cluster(nearby_router_config_);
    nearby_router_cluster.CalculateSet(location, instances_, unhealthy_set_);
    std::vector<Instance *> result_set;  // 根据健康比例选择实例
    ASSERT_TRUE(nearby_router_cluster.CalculateResult(result_set, match_level));
    ASSERT_EQ(result_set.size(), 3);
    ASSERT_EQ(result_set[0], instances_[0]);
    ASSERT_EQ(result_set[1], instances_[1]);
    ASSERT_EQ(result_set[2], instances_[2]);
  }
}

TEST_F(NearbyRouterClusterTest, CalculateLocation) {
  unhealthy_set_.insert(instances_[0]);
  int match_level;
  for (int i = 0; i < 2; ++i) {
    Location location   = {"华南", "深圳", ""};
    std::string content = "matchLevel: campus";
    if (i > 0) content += "\nunhealthyPercentToDegrade: 15";
    ASSERT_TRUE(InitNearbyRouterConfig(nearby_router_config_, content));
    NearbyRouterCluster instances_with_only_zone(nearby_router_config_);
    instances_with_only_zone.CalculateSet(location, instances_, unhealthy_set_);
    ASSERT_EQ(instances_with_only_zone.data_.size(), 4);  // 4个级别分组
    ASSERT_EQ(instances_with_only_zone.data_[kNearbyMatchCampus].healthy_.size(), 0);
    ASSERT_EQ(instances_with_only_zone.data_[kNearbyMatchCampus].unhealthy_.size(), 0);
    ASSERT_EQ(instances_with_only_zone.data_[kNearbyMatchZone].healthy_.size(), 5);
    ASSERT_EQ(instances_with_only_zone.data_[kNearbyMatchZone].unhealthy_.size(), 1);
    ASSERT_EQ(instances_with_only_zone.data_[kNearbyMatchRegion].healthy_.size(), 3);
    ASSERT_EQ(instances_with_only_zone.data_[kNearbyMatchNone].healthy_.size(), 1);
    result_set_.clear();
    if (i == 0) {  // 无匹配实例导致降级
      ASSERT_TRUE(instances_with_only_zone.CalculateResult(result_set_, match_level));
      ASSERT_EQ(result_set_.size(), 5);
    } else {  // 健康率不满足触发降级
      ASSERT_TRUE(instances_with_only_zone.CalculateResult(result_set_, match_level));
      ASSERT_EQ(result_set_.size(), 5 + 3);
    }
  }

  // 只匹配region
  Location location = {"华南", "深圳", "南山"};
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_router_config_, "matchLevel: region"));
  NearbyRouterCluster instances_match_region(nearby_router_config_);
  instances_match_region.CalculateSet(location, instances_, unhealthy_set_);
  ASSERT_EQ(instances_match_region.data_.size(), 2);  // 2个级别分组
  ASSERT_EQ(instances_match_region.data_[kNearbyMatchRegion].healthy_.size(), 8);
  ASSERT_EQ(instances_match_region.data_[kNearbyMatchRegion].unhealthy_.size(), 1);
  ASSERT_EQ(instances_match_region.data_[kNearbyMatchNone].healthy_.size(), 1);
  result_set_.clear();
  instances_match_region.CalculateResult(result_set_, match_level);
  ASSERT_EQ(result_set_.size(), 8);

  // 只匹配region和zone
  ASSERT_TRUE(InitNearbyRouterConfig(nearby_router_config_, "matchLevel: zone"));
  NearbyRouterCluster instances_match_zone(nearby_router_config_);
  instances_match_zone.CalculateSet(location, instances_, unhealthy_set_);
  ASSERT_EQ(instances_match_zone.data_.size(), 3);  // 3个级别分组
  ASSERT_EQ(instances_match_zone.data_[kNearbyMatchZone].healthy_.size(), 5);
  ASSERT_EQ(instances_match_zone.data_[kNearbyMatchZone].unhealthy_.size(), 1);
  ASSERT_EQ(instances_match_zone.data_[kNearbyMatchRegion].healthy_.size(), 3);
  ASSERT_EQ(instances_match_zone.data_[kNearbyMatchNone].healthy_.size(), 1);
  result_set_.clear();
  instances_match_zone.CalculateResult(result_set_, match_level);
  ASSERT_EQ(result_set_.size(), 5);

  // 全部匹配
  for (int i = 10; i <= 40; i += 10) {
    std::string content = "matchLevel: campus\nmaxMatchLevel: region\nunhealthyPercentToDegrade: ";
    content += StringUtils::TypeToStr(i);  // 不健康比例: 10, 20, 30, 40
    ASSERT_TRUE(InitNearbyRouterConfig(nearby_router_config_, content));
    NearbyRouterCluster instances_set(nearby_router_config_);
    instances_set.CalculateSet(location, instances_, unhealthy_set_);
    ASSERT_EQ(instances_set.data_.size(), 4);  // 4个级别分组
    ASSERT_EQ(instances_set.data_[kNearbyMatchCampus].healthy_.size(), 2);
    ASSERT_EQ(instances_set.data_[kNearbyMatchCampus].unhealthy_.size(), 1);
    ASSERT_EQ(instances_set.data_[kNearbyMatchZone].healthy_.size(), 3);
    ASSERT_EQ(instances_set.data_[kNearbyMatchRegion].healthy_.size(), 3);
    ASSERT_EQ(instances_set.data_[kNearbyMatchNone].healthy_.size(), 1);
    result_set_.clear();
    if (i == 10 || i == 40) {  // 降级无法满足10%的比例，退回到campus就近
      ASSERT_FALSE(instances_set.CalculateResult(result_set_, match_level));
      ASSERT_EQ(result_set_.size(), 2);
    } else if (i == 20 || i == 30) {
      ASSERT_TRUE(instances_set.CalculateResult(result_set_, match_level));
      ASSERT_EQ(result_set_.size(), 5);
    }
  }
}

// 就近路由测试
class NearbyServiceRouterTest : public ::testing::Test {
protected:
  virtual void SetUp() {
    service_      = NULL;
    service_data_ = NULL;
    std::string err_msg;
    Config *config  = Config::CreateFromString("matchLevel: campus", err_msg);
    context_        = TestContext::CreateContext();
    service_router_ = new NearbyServiceRouter();
    ASSERT_EQ(service_router_->Init(config, context_), kReturnOk);
    delete config;
  }

  virtual void TearDown() {
    if (context_ != NULL) {
      delete context_;
      context_ = NULL;
    }
    if (service_router_ != NULL) {
      delete service_router_;
      service_router_ = NULL;
    }
    if (service_data_ != NULL) {
      service_data_->DecrementRef();
      service_data_ = NULL;
    }
    if (service_ != NULL) {
      delete service_;
      service_ = NULL;
    }
  }

  static void AddServiceInstance(const std::string &instance_id, const std::string &host, int port,
                                 const std::string &region, const std::string &zone,
                                 const std::string &campus, v1::DiscoverResponse &response);

  std::vector<Instance *> DoRoute(v1::DiscoverResponse &response, const std::string &nearby);

protected:
  Context *context_;
  ServiceRouter *service_router_;
  Service *service_;
  ServiceData *service_data_;
};

void NearbyServiceRouterTest::AddServiceInstance(const std::string &instance_id,
                                                 const std::string &host, int port,
                                                 const std::string &region, const std::string &zone,
                                                 const std::string &campus,
                                                 v1::DiscoverResponse &response) {
  v1::Instance *instance = response.add_instances();
  instance->mutable_id()->set_value(instance_id);
  instance->mutable_host()->set_value(host);
  instance->mutable_port()->set_value(port);
  instance->mutable_weight()->set_value(100);
  instance->mutable_location()->mutable_region()->set_value(region);
  instance->mutable_location()->mutable_zone()->set_value(zone);
  instance->mutable_location()->mutable_campus()->set_value(campus);
}

std::vector<Instance *> NearbyServiceRouterTest::DoRoute(v1::DiscoverResponse &response,
                                                         const std::string &nearby) {
  ServiceKey service_key = {"test_service_namespace", "test_service_name"};
  RouteInfo route_info(service_key, NULL);
  response.set_type(v1::DiscoverResponse::INSTANCE);
  FakeServer::InstancesResponse(response, service_key);
  if (!nearby.empty()) {
    (*(response.mutable_service()->mutable_metadata()))["internal-enable-nearby"] = nearby;
  }
  service_      = new Service(service_key, 0);
  service_data_ = ServiceData::CreateFromPb(&response, kDataInitFromDisk);
  service_->UpdateData(service_data_);
  service_data_->IncrementRef();
  route_info.SetServiceInstances(new ServiceInstances(service_data_));

  RouteResult route_result;
  EXPECT_EQ(service_router_->DoRoute(route_info, &route_result), kReturnOk);

  ServiceInstances *service_instances = route_result.GetServiceInstances();
  EXPECT_TRUE(service_instances != NULL);
  InstancesSet *instances_set = service_instances->GetAvailableInstances();
  return instances_set->GetInstances();
}

TEST_F(NearbyServiceRouterTest, GetFilteredInstancesCampus) {
  Location location = {"华南", "深圳", "深圳-蛇口"};
  context_->GetContextImpl()->GetClientLocation().Update(location);

  v1::DiscoverResponse response;
  AddServiceInstance("instance_1", "127.0.0.1", 8010, "华南", "深圳", "深圳-蛇口", response);
  AddServiceInstance("instance_2", "127.0.0.1", 8020, "华南", "深圳", "深圳-宝安", response);
  AddServiceInstance("instance_3", "127.0.0.1", 8030, "华东", "南京", "南京-软件园", response);
  AddServiceInstance("instance_4", "127.0.0.1", 8040, "华北", "北京", "北京-西北旺", response);

  std::vector<Instance *> instances_set = DoRoute(response, "true");
  ASSERT_EQ(instances_set.size(), 1);
  ASSERT_EQ(instances_set[0]->GetId(), "instance_1");
}

TEST_F(NearbyServiceRouterTest, GetFilteredInstancesNone) {
  Location location = {"西北", "西安", "西安-长安"};
  context_->GetContextImpl()->GetClientLocation().Update(location);

  v1::DiscoverResponse response;
  AddServiceInstance("instance_1", "127.0.0.1", 8010, "华南", "深圳", "深圳-蛇口", response);
  AddServiceInstance("instance_2", "127.0.0.1", 8020, "华南", "广州", "广州-大学城", response);
  AddServiceInstance("instance_3", "127.0.0.1", 8030, "华东", "南京", "南京-软件园", response);
  AddServiceInstance("instance_4", "127.0.0.1", 8040, "华北", "北京", "北京-西北旺", response);

  std::vector<Instance *> instances_set = DoRoute(response, "TRUE");
  ASSERT_EQ(instances_set.size(), 4);
  ASSERT_EQ(instances_set[0]->GetId(), "instance_1");
  ASSERT_EQ(instances_set[1]->GetId(), "instance_2");
  ASSERT_EQ(instances_set[2]->GetId(), "instance_3");
  ASSERT_EQ(instances_set[3]->GetId(), "instance_4");
}

TEST_F(NearbyServiceRouterTest, GetFilteredInstancesZone) {
  Location location = {"华南", "深圳", "深圳-福田"};
  context_->GetContextImpl()->GetClientLocation().Update(location);

  v1::DiscoverResponse response;
  AddServiceInstance("instance_1", "127.0.0.1", 8010, "华南", "广州", "广州-大学城", response);
  AddServiceInstance("instance_2", "127.0.0.1", 8020, "华南", "深圳", "深圳-宝安", response);
  AddServiceInstance("instance_3", "127.0.0.1", 8030, "华东", "南京", "南京-软件园", response);
  AddServiceInstance("instance_4", "127.0.0.1", 8040, "华北", "北京", "北京-西北旺", response);
  AddServiceInstance("instance_5", "127.0.0.1", 8050, "华南", "深圳", "深圳-蛇口", response);

  std::vector<Instance *> instances_set = DoRoute(response, "TRUE");
  ASSERT_EQ(instances_set.size(), 2);
  ASSERT_EQ(instances_set[0]->GetId(), "instance_2");
  ASSERT_EQ(instances_set[1]->GetId(), "instance_5");
}

TEST_F(NearbyServiceRouterTest, GetFilteredInstancesRegion) {
  Location location = {"华南", "深圳", "深圳-福田"};
  context_->GetContextImpl()->GetClientLocation().Update(location);

  v1::DiscoverResponse response;
  AddServiceInstance("instance_1", "127.0.0.1", 8010, "华南", "广州", "", response);
  AddServiceInstance("instance_2", "127.0.0.1", 8020, "华南", "惠州", "惠州-龙门", response);
  AddServiceInstance("instance_3", "127.0.0.1", 8030, "华东", "南京", "南京-软件园", response);
  AddServiceInstance("instance_4", "127.0.0.1", 8040, "华南", "珠海", "珠海-金湾", response);

  std::vector<Instance *> instances_set = DoRoute(response, "TRUE");
  ASSERT_EQ(instances_set.size(), 3);
  ASSERT_EQ(instances_set[0]->GetId(), "instance_1");
  ASSERT_EQ(instances_set[1]->GetId(), "instance_2");
  ASSERT_EQ(instances_set[2]->GetId(), "instance_4");
}

TEST_F(NearbyServiceRouterTest, ServiceDisableNearby) {
  Location location = {"华南", "深圳", "福田"};
  context_->GetContextImpl()->GetClientLocation().Update(location);

  v1::DiscoverResponse response;
  AddServiceInstance("instance_1", "127.0.0.1", 8010, "华南", "深圳", "福田", response);
  AddServiceInstance("instance_2", "127.0.0.1", 8020, "华东", "南京", "软件园", response);

  std::vector<Instance *> instances_set = DoRoute(response, "");
  ASSERT_EQ(instances_set.size(), 2);
  ASSERT_EQ(instances_set[0]->GetId(), "instance_1");
  ASSERT_EQ(instances_set[1]->GetId(), "instance_2");
}

}  // namespace polaris
