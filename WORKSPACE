workspace(name = "polaris_cpp")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# Load common dependencies.
load("//:polaris_deps.bzl", "polaris_deps")
polaris_deps()

http_archive(
    name = "com_google_googletest",
    sha256 = "9bf1fe5182a604b4135edc1a425ae356c9ad15e9b23f9f12a02e80184c3a249c",
    strip_prefix = "googletest-release-1.8.1",
    build_file = "@//third_party/gtest:BUILD",
    urls = ["https://github.com/google/googletest/archive/release-1.8.1.tar.gz"]
)
