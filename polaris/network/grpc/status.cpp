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

#include "status.h"

namespace polaris {
namespace grpc {

GrpcStatusCode StatusCodeUtil::HttpToGrpcStatusCode(uint64_t http_status_code) {
  switch (http_status_code) {
    case 400:
      return kGrpcStatusInternal;
    case 401:
      return kGrpcStatusUnauthenticated;
    case 403:
      return kGrpcStatusPermissionDenied;
    case 404:
      return kGrpcStatusUnimplemented;
    case 429:
    case 502:
    case 503:
    case 504:
      return kGrpcStatusUnavailable;
    default:
      return kGrpcStatusUnknown;
  }
}

// From https://cloud.google.com/apis/design/errors#handling_errors.
uint64_t StatusCodeUtil::GrpcToHttpStatusCode(GrpcStatusCode grpc_status_code) {
  switch (grpc_status_code) {
    case kGrpcStatusOk:
      return 200;
    case kGrpcStatusInvalidArgument:
    case kGrpcStatusFailedPrecondition:
    case kGrpcStatusOutOfRange:
      return 400;  // Bad request.
    case kGrpcStatusUnauthenticated:
      return 401;  // Unauthorized.
    case kGrpcStatusPermissionDenied:
      return 403;  // Forbidden.
    case kGrpcStatusNotFound:
      return 404;  // Not found.
    case kGrpcStatusAlreadyExists:
    case kGrpcStatusAborted:
      return 409;  // Conflict.
    case kGrpcStatusResourceExhausted:
      return 429;  // Too many requests.
    case kGrpcStatusCanceled:
      return 499;  // Client closed request.
    case kGrpcStatusUnimplemented:
      return 501;  // Not implemented.
    case kGrpcStatusUnavailable:
      return 503;  // Service unavailable.
    case kGrpcStatusDeadlineExceeded:
      return 504;  // Gateway Time-out.
    case kGrpcStatusInvalidCode:
    default:
      return 500;  // Internal server error.
  }
}

}  // namespace grpc
}  // namespace polaris
