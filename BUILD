licenses(["notice"])

package(default_visibility = ["//visibility:private"])

load("@com_google_protobuf//:protobuf.bzl", "cc_proto_library")

cc_library(
    name = "murmurhash",
    srcs = glob([
        "third_party/murmurhash/src/MurmurHash3.cpp",
    ]),
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
    visibility = [
        "//visibility:private",
    ],
    linkstatic = 1,
)

cc_library(
    name = "nghttp2",
    srcs = glob([
        "third_party/nghttp2/lib/**/*.c",
    ],
    exclude = ["third_party/nghttp2/lib/**/*_test.c"]),
    hdrs = glob([
        "third_party/nghttp2/lib/includes/**/*.h",
        "third_party/nghttp2/lib/**/*.h",
    ]),
    includes = ["third_party/nghttp2/lib/includes"],
    visibility = [
        "//visibility:private",
    ],
    linkstatic = 1,
)

cc_library(
    name = "re2",
    srcs = glob([
        "third_party/re2/re2/*.cc",
        "third_party/re2/**/*.h",
        "third_party/re2/util/hash.cc",
        "third_party/re2/util/logging.cc",
        "third_party/re2/util/rune.cc",
        "third_party/re2/util/stringprintf.cc",
        "third_party/re2/util/strutil.cc",
        "third_party/re2/util/valgrind.cc",
    ]),
    hdrs = glob(["third_party/re2/**/*.h",]),
    includes = ["third_party/re2"],
    visibility = ["//visibility:private"],
    linkstatic = 1,
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
    deps = [
        ":trpcapi_cc_trpc",
        ":code_cc_proto",
        ":yaml-cpp-polaris-internal",
        ":nghttp2",
        ":murmurhash",
        ":re2",
        ":ratelimit_proto_v2",
    ],
    includes = [
        "include",
        "polaris",
    ],
    visibility = [
        "//visibility:public",
    ],
    copts = [
        "-DREVISION=\\\"trpc-cpp\\\"",
    ],
)

cc_library(
    name = "polaris_api_fork",
    srcs = glob([
        "polaris/**/*.cpp",
        "polaris/**/*.h",
    ]),
    hdrs = glob([
        "include/**/*.h",
    ]),
    deps = [
        ":trpcapi_cc_trpc",
        ":code_cc_proto",
        ":yaml-cpp-polaris-internal",
        ":nghttp2",
        ":murmurhash",
        ":re2",
    ],
    includes = [
        "include",
        "polaris",
    ],
    visibility = [
        "//visibility:public",
    ],
    copts = [
        "-DREVISION=\\\"trpc-cpp\\\"", "-DPOLARIS_SUPPORT_FORK",
    ],
)

cc_proto_library(
    name = "trpcapi_cc_trpc",
    srcs = [
        "polaris/proto/v1/trpcapi.proto",
    ],
    deps = [
        ":service_proto",
        ":routing_proto",
        ":ratelimit_proto",
        ":request_proto",
        ":response_proto",
    ],
    include = "polaris/proto",
    use_grpc_plugin = False,
)

cc_proto_library(
    name = "response_proto",
    srcs = ["polaris/proto/v1/response.proto"],
    deps = [
        ":service_proto",
        ":routing_proto",
        ":ratelimit_proto",
        ":client_proto",
        ":circuit_breaker_proto",
        ":metric_proto",
        "@com_google_protobuf//:cc_wkt_protos"
    ],
    include = "polaris/proto",
)

cc_proto_library(
    name = "client_proto",
    srcs = [":polaris/proto/v1/client.proto"],
    deps = [
        ":model_proto",
        "@com_google_protobuf//:cc_wkt_protos"
    ],
    include = "polaris/proto",
)

cc_proto_library(
    name = "routing_proto",
    srcs = ["polaris/proto/v1/routing.proto"],
    deps = [
        ":model_proto",
        "@com_google_protobuf//:cc_wkt_protos"
    ],
    include = "polaris/proto",
)

cc_proto_library(
    name = "request_proto",
    srcs = ["polaris/proto/v1/request.proto"],
    deps = [
        ":service_proto",
    ],
    include = "polaris/proto",
)

cc_proto_library(
    name = "service_proto",
    srcs = ["polaris/proto/v1/service.proto"],
    deps = [
        ":model_proto",
        "@com_google_protobuf//:cc_wkt_protos"
    ],
    include = "polaris/proto",
)

cc_proto_library(
    name = "ratelimit_proto",
    srcs = ["polaris/proto/v1/ratelimit.proto"],
    deps = [
        ":model_proto",
        "@com_google_protobuf//:cc_wkt_protos"
    ],
    include = "polaris/proto",
)

cc_proto_library(
    name = "ratelimit_proto_v2",
    srcs = ["polaris/proto/v2/ratelimit_v2.proto"],
    deps = [
        "@com_google_protobuf//:cc_wkt_protos"
    ],
    include = "polaris/proto",
)

cc_proto_library(
    name = "circuit_breaker_proto",
    srcs = ["polaris/proto/v1/circuitbreaker.proto"],
    deps = [
            ":model_proto",
            "@com_google_protobuf//:cc_wkt_protos"
        ],
    include = "polaris/proto",
)

cc_proto_library(
    name = "metric_proto",
    srcs = ["polaris/proto/v1/metric.proto"],
    deps = ["@com_google_protobuf//:cc_wkt_protos"],
    include = "polaris/proto",
)

cc_proto_library(
    name = "model_proto",
    srcs = ["polaris/proto/v1/model.proto"],
    deps = [
        "@com_google_protobuf//:cc_wkt_protos"
    ],
    include = "polaris/proto",
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
    deps = [
        ":trpcapi_cc_trpc",
        ":code_cc_proto",
        ":yaml-cpp-polaris-internal",
        ":nghttp2",
        ":murmurhash",
        ":re2",
        ":ratelimit_proto_v2",
    ],
    includes = [
        "include",
        "polaris",
    ],
    visibility = [
        "//visibility:public",
    ],
)

