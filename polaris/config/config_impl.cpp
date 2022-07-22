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
#include "config_impl.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "logger.h"
#include "polaris/config.h"
#include "utils/string_utils.h"

namespace polaris {

void SplitString(const std::string& value, std::vector<std::string>& value_list, char delimiter) {
  if (value.empty()) {
    return;
  }
  std::string sub_value = value;
  std::size_t index = sub_value.find(delimiter);
  while (index != std::string::npos) {
    value_list.push_back(StringUtils::StringTrim(sub_value.substr(0, index)));
    sub_value = sub_value.substr(index + 1);
    index = sub_value.find(delimiter);
  }
  value_list.push_back(StringUtils::StringTrim(sub_value));
}

Config* Config::GetSubConfig(const std::string& key) {
  static YAML_0_3::Node empty_node;
  POLARIS_ASSERT(impl_ != nullptr);
  if (impl_->emitter_ != nullptr) {
    POLARIS_ASSERT(impl_->json_emitter_ != nullptr);
    *impl_->emitter_ << YAML_0_3::Key << key << YAML_0_3::Value;
    *impl_->json_emitter_ << YAML_0_3::Key << key << YAML_0_3::Value;
  }
  if (impl_->data_->Type() == YAML_0_3::NodeType::Null) {
    return new Config(new ConfigImpl(&empty_node, impl_->emitter_, impl_->json_emitter_, key));
  } else if (impl_->data_->Type() == YAML_0_3::NodeType::Map) {
    const YAML_0_3::Node* sub_data = impl_->data_->FindValue(key);
    if (sub_data != nullptr) {
      return new Config(
          new ConfigImpl(const_cast<YAML_0_3::Node*>(sub_data), impl_->emitter_, impl_->json_emitter_, key));
    } else {
      return new Config(new ConfigImpl(&empty_node, impl_->emitter_, impl_->json_emitter_, key));
    }
  } else {
    std::string err_msg = "get sub config " + key + " from error type";
    throw new YAML_0_3::ParserException(YAML_0_3::Mark::null(), err_msg);
  }
}

bool Config::SubConfigExist(const std::string& key) {
  return impl_->data_->Type() == YAML_0_3::NodeType::Map && impl_->data_->FindValue(key) != nullptr;
}

std::vector<Config*> Config::GetSubConfigList(const std::string& key) {
  POLARIS_ASSERT(impl_ != nullptr);
  std::vector<Config*> value;
  if (impl_->data_->Type() == YAML_0_3::NodeType::Map) {
    const YAML_0_3::Node* sub_data = impl_->data_->FindValue(key);
    if (sub_data != nullptr && sub_data->Type() == YAML_0_3::NodeType::Sequence) {
      for (auto it = sub_data->begin(); it != sub_data->end(); ++it) {
        value.push_back(new Config(new ConfigImpl(it->Clone().release())));
      }
      if (impl_->emitter_ != nullptr) {
        POLARIS_ASSERT(impl_->json_emitter_ != nullptr);
        *impl_->emitter_ << YAML_0_3::Key << key << YAML_0_3::Value << *sub_data;
        *impl_->json_emitter_ << YAML_0_3::Key << key << YAML_0_3::Value << *sub_data;
      }
    }
  }
  return value;
}

Config* Config::GetSubConfigClone(const std::string& key) {
  POLARIS_ASSERT(impl_ != nullptr);
  if (impl_->data_->Type() == YAML_0_3::NodeType::Null) {
    return new Config(new ConfigImpl(new YAML_0_3::Node()));
  } else if (impl_->data_->Type() == YAML_0_3::NodeType::Map) {
    const YAML_0_3::Node* sub_data = impl_->data_->FindValue(key);
    if (sub_data != nullptr) {
      return new Config(new ConfigImpl(sub_data->Clone().release()));
    } else {
      return new Config(new ConfigImpl(new YAML_0_3::Node()));
    }
  } else {
    std::string err_msg = "clone sub config " + key + " from error type";
    throw new YAML_0_3::ParserException(YAML_0_3::Mark::null(), err_msg);
  }
}

template <typename T>
T GetOrDefault(ConfigImpl* impl_, const std::string& key, const T& default_value) {
  POLARIS_ASSERT(impl_ != nullptr);
  T value = default_value;
  if (impl_->data_->Type() == YAML_0_3::NodeType::Map) {
    const YAML_0_3::Node* sub_data = impl_->data_->FindValue(key);
    if (sub_data != nullptr) {
      value = sub_data->to<T>();
    }
  }
  if (impl_->emitter_ != nullptr) {
    POLARIS_ASSERT(impl_->json_emitter_ != nullptr);
    *impl_->emitter_ << YAML_0_3::Key << key << YAML_0_3::Value << value;
    *impl_->json_emitter_ << YAML_0_3::Key << key << YAML_0_3::Value << value;
  }
  return value;
}

std::string Config::GetStringOrDefault(const std::string& key, const std::string& default_value) {
  return GetOrDefault<std::string>(impl_, key, default_value);
}

int Config::GetIntOrDefault(const std::string& key, int default_value) {
  return GetOrDefault<int>(impl_, key, default_value);
}

bool Config::GetBoolOrDefault(const std::string& key, bool default_value) {
  return GetOrDefault<bool>(impl_, key, default_value);
}

float Config::GetFloatOrDefault(const std::string& key, float default_value) {
  return GetOrDefault<float>(impl_, key, default_value);
}

bool ParseTimeValue(std::string& time_value, uint64_t& result) {
  uint64_t base = 1;
  if (time_value.length() >= 2) {
    if (time_value[time_value.length() - 1] == 'h') {  // hour
      time_value = time_value.substr(0, time_value.length() - 1);
      base = 60 * 60 * 1000;
    } else if (time_value[time_value.length() - 1] == 'm') {  // minute
      time_value = time_value.substr(0, time_value.length() - 1);
      base = 60 * 1000;
    } else if (time_value[time_value.length() - 1] == 's') {
      if (time_value[time_value.length() - 2] == 'm') {  // millsecond
        time_value = time_value.substr(0, time_value.length() - 2);
      } else {  // second
        time_value = time_value.substr(0, time_value.length() - 1);
        base = 1000;
      }
    }
  }
  result = 0;
  for (std::size_t i = 0; i < time_value.size(); ++i) {
    if (isdigit(time_value[i])) {
      result = result * 10 + (time_value[i] - '0');
    } else {
      return false;
    }
  }
  result = result * base;
  return true;
}

uint64_t Config::GetMsOrDefault(const std::string& key, uint64_t default_value) {
  POLARIS_ASSERT(impl_ != nullptr);
  if (impl_->data_->Type() == YAML_0_3::NodeType::Map) {
    const YAML_0_3::Node* sub_data = impl_->data_->FindValue(key);
    if (sub_data != nullptr) {
      std::string time_value = sub_data->to<std::string>();
      if (impl_->emitter_ != nullptr) {
        POLARIS_ASSERT(impl_->json_emitter_ != nullptr);
        *impl_->emitter_ << YAML_0_3::Key << key << YAML_0_3::Value << time_value;
        *impl_->json_emitter_ << YAML_0_3::Key << key << YAML_0_3::Value << time_value;
      }
      uint64_t result;
      if (ParseTimeValue(time_value, result)) {
        return result;
      } else {
        throw YAML_0_3::InvalidScalar(sub_data->GetMark());
      }
    }
  }
  if (impl_->emitter_ != nullptr) {
    POLARIS_ASSERT(impl_->json_emitter_ != nullptr);
    *impl_->emitter_ << YAML_0_3::Key << key << YAML_0_3::Value << default_value;
    *impl_->json_emitter_ << YAML_0_3::Key << key << YAML_0_3::Value << default_value;
  }
  return default_value;
}

std::vector<std::string> Config::GetListOrDefault(const std::string& key, const std::string& default_value) {
  POLARIS_ASSERT(impl_ != nullptr);
  std::vector<std::string> value;
  if (impl_->data_->Type() == YAML_0_3::NodeType::Map) {
    const YAML_0_3::Node* sub_data = impl_->data_->FindValue(key);
    if (sub_data != nullptr) {
      value = sub_data->to<std::vector<std::string> >();
    } else {
      SplitString(default_value, value, ',');
    }
  } else {
    SplitString(default_value, value, ',');
  }
  if (impl_->emitter_ != nullptr) {
    POLARIS_ASSERT(impl_->json_emitter_ != nullptr);
    *impl_->emitter_ << YAML_0_3::Key << key << YAML_0_3::Value << value;
    *impl_->json_emitter_ << YAML_0_3::Key << key << YAML_0_3::Value << value;
  }
  return value;
}

std::map<std::string, std::string> Config::GetMap(const std::string& key) {
  POLARIS_ASSERT(impl_ != nullptr);
  std::map<std::string, std::string> value;
  if (impl_->data_->Type() == YAML_0_3::NodeType::Map) {
    const YAML_0_3::Node* sub_data = impl_->data_->FindValue(key);
    if (sub_data != nullptr) {
      value = sub_data->to<std::map<std::string, std::string> >();
    }
  }
  if (impl_->emitter_ != nullptr) {
    POLARIS_ASSERT(impl_->json_emitter_ != nullptr);
    *impl_->emitter_ << YAML_0_3::Key << key << YAML_0_3::Value << value;
    *impl_->json_emitter_ << YAML_0_3::Key << key << YAML_0_3::Value << value;
  }
  return value;
}

const std::string& Config::GetRootKey() const { return impl_->root_; }

std::string Config::ToString() {
  POLARIS_ASSERT(impl_ != nullptr);
  POLARIS_ASSERT(impl_->is_sub_config_ == false);
  POLARIS_ASSERT(impl_->emitter_ != nullptr);
  *impl_->emitter_ << YAML_0_3::EndMap;
  POLARIS_ASSERT(impl_->emitter_->good());
  return impl_->emitter_->c_str();
}

std::string Config::ToJsonString() {
  POLARIS_ASSERT(impl_ != nullptr);
  POLARIS_ASSERT(impl_->is_sub_config_ == false);
  POLARIS_ASSERT(impl_->json_emitter_ != nullptr);
  *impl_->json_emitter_ << YAML_0_3::EndMap;
  POLARIS_ASSERT(impl_->json_emitter_->good());
  return impl_->json_emitter_->c_str();
}

ConfigImpl::ConfigImpl() {
  is_sub_config_ = false;
  data_ = new YAML_0_3::Node();
  emitter_ = new YAML_0_3::Emitter();
  *emitter_ << YAML_0_3::BeginMap;
  json_emitter_ = new YAML_0_3::Emitter();
  json_emitter_->SetStringFormat(YAML_0_3::DoubleQuoted);
  json_emitter_->SetMapFormat(YAML_0_3::Flow);
  *json_emitter_ << YAML_0_3::BeginMap;
}

ConfigImpl::ConfigImpl(YAML_0_3::Node* data) {
  is_sub_config_ = false;
  data_ = data;
  emitter_ = nullptr;
  json_emitter_ = nullptr;
}

ConfigImpl::ConfigImpl(YAML_0_3::Node* data, YAML_0_3::Emitter* emitter, YAML_0_3::Emitter* json_emitter,
                       const std::string& key) {
  is_sub_config_ = true;
  data_ = data;
  emitter_ = emitter;
  json_emitter_ = json_emitter;
  root_ = key;
  if (emitter_ != nullptr) {
    *emitter_ << YAML_0_3::BeginMap;
  }
  if (json_emitter_ != nullptr) {
    *json_emitter_ << YAML_0_3::BeginMap;
  }
}

ConfigImpl::~ConfigImpl() {
  if (is_sub_config_) {
    if (emitter_ != nullptr) {
      *emitter_ << YAML_0_3::EndMap;
    }
    if (json_emitter_ != nullptr) {
      *json_emitter_ << YAML_0_3::EndMap;
    }
  } else {
    if (data_ != nullptr) {
      delete data_;
      data_ = nullptr;
    }
    if (emitter_ != nullptr) {
      delete emitter_;
      emitter_ = nullptr;
    }
    if (json_emitter_ != nullptr) {
      delete json_emitter_;
      json_emitter_ = nullptr;
    }
  }
}

}  // namespace polaris
