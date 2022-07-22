"""Load dependencies needed to compile the polaris library as a 3rd-party consumer."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def polaris_deps():
    """Loads common dependencies needed to compile the polaris library."""

    if not native.existing_rule("com_google_protobuf"):
        http_archive(
            name = "com_google_protobuf",
            sha256 = "03d2e5ef101aee4c2f6ddcf145d2a04926b9c19e7086944df3842b1b8502b783",
            strip_prefix = "protobuf-3.8.0",
            urls = [
                "https://mirror.bazel.build/github.com/protocolbuffers/protobuf/archive/v3.8.0.tar.gz",
                "https://github.com/protocolbuffers/protobuf/archive/v3.8.0.tar.gz",
            ],
        )

    if not native.existing_rule("zlib"):
        http_archive(
            name = "zlib",
            build_file = "@com_google_protobuf//:third_party/zlib.BUILD",
            sha256 = "629380c90a77b964d896ed37163f5c3a34f6e6d897311f1df2a7016355c45eff",
            strip_prefix = "zlib-1.2.11",
            urls = ["https://github.com/madler/zlib/archive/v1.2.11.tar.gz"],
        )

    if not native.existing_rule("bazel_skylib"):
        http_archive(
            name = "bazel_skylib",
            sha256 = "bbccf674aa441c266df9894182d80de104cabd19be98be002f6d478aaa31574d",
            strip_prefix = "bazel-skylib-2169ae1c374aab4a09aa90e65efe1a3aad4e279b",
            urls = [
                "https://github.com/bazelbuild/bazel-skylib/archive/2169ae1c374aab4a09aa90e65efe1a3aad4e279b.tar.gz",
            ],
        )

    if not native.existing_rule("com_googlesource_code_re2"):
        http_archive(
            name = "com_googlesource_code_re2",
            sha256 = "0915741f524ad87debb9eb0429fe6016772a1569e21dc6d492039562308fcb0f",
            strip_prefix = "re2-2020-10-01",
            urls = ["https://github.com/google/re2/archive/2020-10-01.tar.gz"],
        )

    if not native.existing_rule("com_googlesource_code_re2"):
        http_archive(
            name = "com_googlesource_code_re2",
            sha256 = "0915741f524ad87debb9eb0429fe6016772a1569e21dc6d492039562308fcb0f",
            strip_prefix = "re2-2020-10-01",
            urls = ["https://github.com/google/re2/archive/2020-10-01.tar.gz"],
        )
