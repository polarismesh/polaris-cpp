load("@com_google_protobuf//:protobuf.bzl", "cc_proto_library")

licenses(["notice"])

package(default_visibility = ["//visibility:private"])

cc_library(
    name = "murmurhash",
    srcs = ["third_party/murmurhash/src/MurmurHash3.cpp"],
    hdrs = glob([
        "third_party/murmurhash/src/**/*.h",
    ]),
    includes = ["third_party/murmurhash/src"],
    linkstatic = 1,
)

cc_library(
    name = "yaml-cpp-polaris-internal",
    srcs = glob([
        "third_party/yaml-cpp/src/**/*.cpp",
        "third_party/yaml-cpp/src/**/*.h",
    ]),
    hdrs = glob([
        "third_party/yaml-cpp/include/**/*.h",
    ]),
    includes = [
        "third_party/yaml-cpp/include",
    ],
    linkstatic = 1,
    visibility = [
        "//visibility:private",
    ],
)

cc_library(
    name = "nghttp2",
    srcs = glob(
        [
            "third_party/nghttp2/lib/**/*.c",
        ],
        exclude = ["third_party/nghttp2/lib/**/*_test.c"],
    ),
    hdrs = glob([
        "third_party/nghttp2/lib/includes/**/*.h",
        "third_party/nghttp2/lib/**/*.h",
    ]),
    includes = ["third_party/nghttp2/lib/includes"],
    linkstatic = 1,
    visibility = [
        "//visibility:private",
    ],
)

cc_library(
    name = "polaris_api",
    srcs = glob([
        "polaris/**/*.cpp",
        "polaris/**/*.h",
    ]),
    hdrs = glob([
        "include/**/*.h",
    ]),
    includes = [
        "include",
        "polaris",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        ":request_proto",
        ":response_proto",
        ":code_cc_proto",
        ":murmurhash",
        ":nghttp2",
        ":ratelimit_proto_v2",
        ":yaml-cpp-polaris-internal",
        "@com_googlesource_code_re2//:re2",
    ],
)

alias(
    name = "polaris_api_fork",
    actual = ":polaris_api",
    visibility = [
        "//visibility:public",
    ],
)

alias(
    name = "polaris_api_trpc",
    actual = ":polaris_api",
    visibility = [
        "//visibility:public",
    ],
)

cc_proto_library(
    name = "response_proto",
    srcs = ["polaris/proto/v1/response.proto"],
    include = "polaris/proto",
    deps = [
        ":circuit_breaker_proto",
        ":client_proto",
        ":metric_proto",
        ":ratelimit_proto",
        ":routing_proto",
        ":service_proto",
        "@com_google_protobuf//:cc_wkt_protos",
    ],
)

cc_proto_library(
    name = "client_proto",
    srcs = [":polaris/proto/v1/client.proto"],
    include = "polaris/proto",
    deps = [
        ":model_proto",
        "@com_google_protobuf//:cc_wkt_protos",
    ],
)

cc_proto_library(
    name = "routing_proto",
    srcs = ["polaris/proto/v1/routing.proto"],
    include = "polaris/proto",
    deps = [
        ":model_proto",
        "@com_google_protobuf//:cc_wkt_protos",
    ],
)

cc_proto_library(
    name = "request_proto",
    srcs = ["polaris/proto/v1/request.proto"],
    include = "polaris/proto",
    deps = [
        ":service_proto",
    ],
)

cc_proto_library(
    name = "service_proto",
    srcs = ["polaris/proto/v1/service.proto"],
    include = "polaris/proto",
    deps = [
        ":model_proto",
        "@com_google_protobuf//:cc_wkt_protos",
    ],
)

cc_proto_library(
    name = "ratelimit_proto",
    srcs = ["polaris/proto/v1/ratelimit.proto"],
    include = "polaris/proto",
    deps = [
        ":model_proto",
        "@com_google_protobuf//:cc_wkt_protos",
    ],
)

cc_proto_library(
    name = "ratelimit_proto_v2",
    srcs = ["polaris/proto/v2/ratelimit_v2.proto"],
    include = "polaris/proto",
    deps = [
        "@com_google_protobuf//:cc_wkt_protos",
    ],
)

cc_proto_library(
    name = "circuit_breaker_proto",
    srcs = ["polaris/proto/v1/circuitbreaker.proto"],
    include = "polaris/proto",
    deps = [
        ":model_proto",
        "@com_google_protobuf//:cc_wkt_protos",
    ],
)

cc_proto_library(
    name = "metric_proto",
    srcs = ["polaris/proto/v1/metric.proto"],
    include = "polaris/proto",
    deps = ["@com_google_protobuf//:cc_wkt_protos"],
)

cc_proto_library(
    name = "model_proto",
    srcs = ["polaris/proto/v1/model.proto"],
    include = "polaris/proto",
    deps = [
        "@com_google_protobuf//:cc_wkt_protos",
    ],
)

cc_proto_library(
    name = "code_cc_proto",
    srcs = ["polaris/proto/v1/code.proto"],
    include = "polaris/proto",
)

cc_library(
    name = "test",
    srcs = glob([
        "test/**/*.cpp",
        "test/**/*.h",
    ]),
    hdrs = glob([
        "include/**/*.h",
    ]),
    includes = [
        "include",
        "polaris",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        ":code_cc_proto",
        ":murmurhash",
        ":nghttp2",
        ":ratelimit_proto_v2",
        ":yaml-cpp-polaris-internal",
        "@com_googlesource_code_re2//:re2",
    ],
)
