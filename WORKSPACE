workspace(name = "polaris_cpp")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "com_google_protobuf",
    sha256 = "03d2e5ef101aee4c2f6ddcf145d2a04926b9c19e7086944df3842b1b8502b783",
    strip_prefix = "protobuf-3.8.0",
    urls = [
        "https://mirror.bazel.build/github.com/protocolbuffers/protobuf/archive/v3.8.0.tar.gz",
        "https://github.com/protocolbuffers/protobuf/archive/v3.8.0.tar.gz",
    ],
)

http_archive(
    name = "zlib",
    build_file = "@com_google_protobuf//:third_party/zlib.BUILD",
    sha256 = "629380c90a77b964d896ed37163f5c3a34f6e6d897311f1df2a7016355c45eff",
    strip_prefix = "zlib-1.2.11",
    urls = ["https://github.com/madler/zlib/archive/v1.2.11.tar.gz"],
)

http_archive(
    name = "com_google_googletest",
    sha256 = "9bf1fe5182a604b4135edc1a425ae356c9ad15e9b23f9f12a02e80184c3a249c",
    strip_prefix = "googletest-release-1.8.1",
    build_file = "@//third_party/gtest:BUILD",
    urls = ["https://github.com/google/googletest/archive/release-1.8.1.tar.gz"]
)

http_archive(
    name = "bazel_skylib",
    sha256 = "bbccf674aa441c266df9894182d80de104cabd19be98be002f6d478aaa31574d",
    strip_prefix = "bazel-skylib-2169ae1c374aab4a09aa90e65efe1a3aad4e279b",
    urls = [
        "https://github.com/bazelbuild/bazel-skylib/archive/2169ae1c374aab4a09aa90e65efe1a3aad4e279b.tar.gz",
    ],
)

#http_archive(
#    name = "com_github_jbeder_yaml_cpp",
#    build_file = "@//:bazel-deps/yaml-cpp.BUILD",
#	sha256 = "e4d8560e163c3d875fd5d9e5542b5fd5bec810febdcba61481fe5fc4e6b1fd05",
#	strip_prefix = "yaml-cpp-yaml-cpp-0.6.2",
#	urls = ["https://github.com/jbeder/yaml-cpp/archive/yaml-cpp-0.6.2.tar.gz"],
#)

#http_archive(
#    name = "com_github_nghttp2",
#    build_file = "@//:bazel-deps/nghttp2.BUILD",
#    sha256 = "63de2828bf6a43a501a982140b7ee969b8118f80351a9e43a1432b0979221957",
#    strip_prefix = "nghttp2-1.40.0",
#    urls = ["https://github.com/nghttp2/nghttp2/archive/v1.40.0.tar.gz"],    
#)
