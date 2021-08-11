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

#ifndef POLARIS_CPP_TEST_TEST_CONTEXT_H_
#define POLARIS_CPP_TEST_TEST_CONTEXT_H_

#include <string>

#include "mock/mock_local_registry.h"
#include "mock/mock_server_connector.h"

#include "context_internal.h"

namespace polaris {

extern std::string g_test_persist_dir_;

class TestContext {
public:
  static Context *CreateContext(std::string server_address = "['Fake:42']",
                                ContextMode mode           = kShareContextWithoutEngine) {
    std::string err_msg, content =
                             "global:\n"
                             "  serverConnector:\n"
                             "    addresses: " +
                             server_address +
                             "\nconsumer:\n"
                             "  localCache:\n"
                             "    persistDir: " +
                             g_test_persist_dir_;
    if (mode == kLimitContext) {
      content +=
          "\nrateLimiter:\n"
          "  mode: local\n";
    }
    Config *config = Config::CreateFromString(content, err_msg);
    POLARIS_ASSERT(config != NULL && err_msg.empty());
    Context *context = Context::Create(config, mode);
    if (mode == kShareContextWithoutEngine) {
      Time::TryShutdomClock();  // 停止clock线程
    }
    delete config;
    return context;
  }

  static Context *CreateContext(ContextMode mode) { return CreateContext("['Fake:42']", mode); }

  static MockLocalRegistry *SetupMockLocalRegistry(Context *context) {
    ContextImpl *context_impl              = context->GetContextImpl();
    LocalRegistry *old_local_registry      = context_impl->local_registry_;
    MockLocalRegistry *mock_local_registry = new MockLocalRegistry();
    context_impl->local_registry_          = mock_local_registry;
    delete old_local_registry;
    old_local_registry = NULL;
    return mock_local_registry;
  }

  static MockServerConnector *SetupMockServerConnector(Context *context) {
    ContextImpl *context_impl                  = context->GetContextImpl();
    ServerConnector *old_server_connector      = context_impl->server_connector_;
    MockServerConnector *mock_server_connector = new MockServerConnector();
    context_impl->server_connector_            = mock_server_connector;
    delete old_server_connector;
    old_server_connector = NULL;
    return mock_server_connector;
  }
};

}  // namespace polaris

#endif  // POLARIS_CPP_TEST_TEST_CONTEXT_H_
