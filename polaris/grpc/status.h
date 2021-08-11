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

#ifndef POLARIS_CPP_POLARIS_GRPC_STATUS_H_
#define POLARIS_CPP_POLARIS_GRPC_STATUS_H_

#include <stdint.h>

namespace polaris {
namespace grpc {

static const uint64_t kHttp2StatusOk = 200;

// Grpc返回状态码，参考 https://github.com/grpc/grpc/blob/master/doc/statuscodes.md
enum GrpcStatusCode {
  kGrpcStatusOk                 = 0,   // The RPC completed successfully.
  kGrpcStatusCanceled           = 1,   // The RPC was canceled.
  kGrpcStatusUnknown            = 2,   // Some unknown error occurred.
  kGrpcStatusInvalidArgument    = 3,   // An argument to the RPC was invalid.
  kGrpcStatusDeadlineExceeded   = 4,   // The deadline for the RPC expired before the RPC completed.
  kGrpcStatusNotFound           = 5,   // Some resource for the RPC was not found.
  kGrpcStatusAlreadyExists      = 6,   // A resource the RPC attempted to create already exists.
  kGrpcStatusPermissionDenied   = 7,   // Permission was denied for the RPC.
  kGrpcStatusResourceExhausted  = 8,   // Some resource is exhausted, resulting in RPC failure.
  kGrpcStatusFailedPrecondition = 9,   // Some precondition for the RPC failed.
  kGrpcStatusAborted            = 10,  // The RPC was aborted.
  kGrpcStatusOutOfRange         = 11,  // Some operation was requested outside of a legal range.
  kGrpcStatusUnimplemented      = 12,  // The RPC requested was not implemented.
  kGrpcStatusInternal           = 13,  // Some internal error occurred.
  kGrpcStatusUnavailable        = 14,  // The RPC endpoint is current unavailable.
  kGrpcStatusDataLoss           = 15,  // There was some data loss resulting in RPC failure.
  kGrpcStatusUnauthenticated    = 16,  // The RPC does not have required credentials.
  kGrpcStatusMaximumValid = kGrpcStatusUnauthenticated,  // Maximum value of valid status codes.
  kGrpcStatusInvalidCode  = -1,  //  The status code in gRPC headers was invalid.
};

class StatusCodeUtil {
public:
  /// @brief
  /// 由于GRPC内部使用HTTP2实现，正常情况下HTTP请求会返回状态码200，并在GRPC应答中包含GRPC状态码
  /// 对于没有GRPC状态码的时候，通过HTTP状态码映射得到GRPC状态码
  /// 参考：https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md
  static GrpcStatusCode HttpToGrpcStatusCode(uint64_t http_status_code);

  /// @brief Grpc状态码转换成HTTP状态码
  static uint64_t GrpcToHttpStatusCode(GrpcStatusCode grpc_status_code);
};

}  // namespace grpc
}  // namespace polaris

#endif  //  POLARIS_CPP_POLARIS_GRPC_STATUS_H_
