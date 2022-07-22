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

#include "plugin/server_connector/server_connector.h"

#include "context/context_impl.h"
#include "logger.h"

namespace polaris {

const char* PolarisRequestTypeStr(PolarisRequestType request_type) {
  switch (request_type) {
    case kBlockRegisterInstance:
      return "RegisterInstance";
    case kBlockDeregisterInstance:
      return "DeregisterInstance";
    case kPolarisHeartbeat:
      return "Heartbeat";
    case kPolarisReportClient:
      return "ReportClient";
    default:
      POLARIS_ASSERT(false);
      return "";
  }
}

const ServiceKey& GetPolarisService(Context* context, PolarisRequestType request_type) {
  switch (request_type) {
    case kBlockRegisterInstance:    // fall through
    case kBlockDeregisterInstance:  // fall through
    case kPolarisReportClient:      // fall through
      return context->GetContextImpl()->GetDiscoverService().service_;
    case kPolarisHeartbeat:
      return context->GetContextImpl()->GetHeartbeatService().service_;
    default:
      POLARIS_ASSERT(false);
      return context->GetContextImpl()->GetDiscoverService().service_;
  }
}

}  // namespace polaris
