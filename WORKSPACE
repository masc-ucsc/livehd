# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

workspace(name = "livehd")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

http_archive(
    name = "rules_foreign_cc",
    sha256 = "5303e3363fe22cbd265c91fce228f84cf698ab0f98358ccf1d95fba227b308f6",
    strip_prefix = "rules_foreign_cc-0.9.0",
    url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.9.0.zip",
)

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

# rules_hdl
#http_archive(
    #name = "rules_python",
    #sha256 = "a644da969b6824cc87f8fe7b18101a8a6c57da5db39caa6566ec6109f37d2141",
    #url = "https://github.com/bazelbuild/rules_python/releases/download/0.20.0/rules_python-0.20.0.tar.gz",
#)

#http_archive(
#    name = "rules_hdl",
#    sha256 = "2326431c5eb66c7e4be8a2d2f89ad6aa929e99727a487e092d9d78e23485ec25",
#    strip_prefix = "bazel_rules_hdl-89b7ff96dfbc69dc653dac2e4015bb39221b2715",
#    url = "https://github.com/masc-ucsc/bazel_rules_hdl/archive/89b7ff96dfbc69dc653dac2e4015bb39221b2715.zip",
#)
http_archive(
    name = "rules_hdl",
    sha256 = "9b3ef2d8e0603de6c689077b75b1fbfa42a24b1410423dd69271130586e2d8ee",
    strip_prefix = "bazel_rules_hdl-4c634c7d2b026870ecbc2fb3c4d463b6bd5c2ceb",
    url = "https://github.com/masc-ucsc/bazel_rules_hdl/archive/4c634c7d2b026870ecbc2fb3c4d463b6bd5c2ceb.zip",
)

load("@rules_hdl//dependency_support:dependency_support.bzl", "dependency_support")

dependency_support()

load("@rules_hdl//:init.bzl", "init")

init()

# mustache
http_archive(
    name = "mustache",
    build_file = "mustache.BUILD",
    sha256 = "d74b503baedd98a51bf8142355ef9ce55daf2268150e5897f6629f2d0f9205e2",
    strip_prefix = "Mustache-04277d5552c6e46bee41a946b7d175a660ea1b3d",
    urls = [
        "https://github.com/kainjow/Mustache/archive/04277d5552c6e46bee41a946b7d175a660ea1b3d.zip",
    ],
)

# google benchmark
http_archive(
    name = "com_google_benchmark",
    sha256 = "927475805a19b24f9c67dd9765bb4dcc8b10fd7f0e616905cc4fee406bed81a7",
    strip_prefix = "benchmark-1.8.0",
    urls = ["https://github.com/google/benchmark/archive/refs/tags/v1.8.0.zip"],
)

# fmt
# http_archive(
#     name = "fmt",
#     build_file = "fmt.BUILD",
#     sha256 = "fccfc86e4aff6c33bff9a1d907b2ba2de2a5a8ab84349be4964a93f8d8c21b62",
#     strip_prefix = "fmt-7bdf0628b1276379886c7f6dda2cef2b3b374f0b",
#     urls = [
#         "https://github.com/fmtlib/fmt/archive/7bdf0628b1276379886c7f6dda2cef2b3b374f0b.zip",
#     ],
# )
http_archive(
    name = "fmt",
    build_file = "fmt.BUILD",
    sha256 = "5bf4d5358301fdf3bd100c01b9d4c1fbb2091dc2267fb4fa6d7cd522b3e47179",
    strip_prefix = "fmt-10.0.0",
    urls = [
        "https://github.com/fmtlib/fmt/archive/refs/tags/10.0.0.zip",
    ],
)

# slang
http_archive(
    name = "slang",
    build_file = "slang.BUILD",
    patches = ["//external:slang.patch"],
    sha256 = "28263ac3653b8b219ae4e9b9ab0e2b2603bb014c45399db8b85ff2a1ebf3e173",
    strip_prefix = "slang-6f2b66270064f40501754f64883f83bc5e7ca5a6",
    urls = [
        "https://github.com/masc-ucsc/slang/archive/6f2b66270064f40501754f64883f83bc5e7ca5a6.zip",
    ],
)

# tree-sitter-pyrope
new_git_repository(
    name = "tree-sitter-pyrope",
    branch = "main",
    build_file = "tree-sitter-pyrope.BUILD",
    remote = "https://github.com/masc-ucsc/tree-sitter-pyrope",
)

# tree sitter
http_archive(
    name = "tree-sitter",
    build_file = "tree-sitter.BUILD",
    sha256 = "5f04d75f2b2b9424131e4b769c1e64f7a82bfcf930c79a87c118d251d44ef6e2",
    strip_prefix = "tree-sitter-0.20.8",
    urls = [
      "https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v0.20.8.zip"
    ],
)

# nlohmann json
http_archive(
    name = "json",
    build_file = "json.BUILD",
    sha256 = "95651d7d1fcf2e5c3163c3d37df6d6b3e9e5027299e6bd050d157322ceda9ac9",
    strip_prefix = "json-3.11.2",
    urls = [
      "https://github.com/nlohmann/json/archive/refs/tags/v3.11.2.zip"
    ]
)

# iassert
http_archive(
    name = "iassert",
    sha256 = "c6bf66a76d5a1de57c45dba137c9b51ab3b4f3a31e5de9e3c3496d7d36a128f8",
    strip_prefix = "iassert-5c18eb082262532f621a23023f092f4119a44968",
    urls = [
        "https://github.com/masc-ucsc/iassert/archive/5c18eb082262532f621a23023f092f4119a44968.zip",
    ],
)

# HIF
http_archive(
    name = "hif",
    # sha256 = "9eee32fcb0ec4bbe9ed945de55c7161e294e76958af2461c8544ea5c1e484d02",
    strip_prefix = "hif-aef1cb9b1b72ad6a0edd58685c657f6eafef7446",
    urls = [
        "https://github.com/masc-ucsc/hif/archive/aef1cb9b1b72ad6a0edd58685c657f6eafef7446.zip",
    ],
)

# cryptominisat
http_archive(
    name = "cryptominisat",
    build_file = "cryptominisat.BUILD",
    patches = ["//external:cryptominisat.patch"],
    sha256 = "9a3298bf8b7eebb7ab1fa19ae6a83fda194b3e8d2dd409db0bb8fbc7e73cff1e",
    strip_prefix = "cryptominisat-5.11.4",
    urls = [
      "https://github.com/msoos/cryptominisat/archive/refs/tags/5.11.4.zip",
    ],
)

# boolector
# The last release is quite old. So top of tree
http_archive(
    name = "boolector",
    build_file = "boolector.BUILD",
    patches = ["//external:boolector.patch"],
    sha256 = "d955193f3b32f1a86646d744b8cb688d00d2e2c1c690d249b1041dc481315bbe",
    strip_prefix = "boolector-13a8a06d561041cafcaf5458e404c1ec354b2841",
    urls = [
        "https://github.com/Boolector/boolector/archive/13a8a06d561041cafcaf5458e404c1ec354b2841.zip",
    ],
)

# rapidjson
# The last release is quite old. So top of tree
http_archive(
    name = "rapidjson",
    build_file = "rapidjson.BUILD",
    sha256 = "d556fa2bc7bf650e4ec61668e46b8acc04959d23e9f429dd69c6d1e945b3ab5c",
    strip_prefix = "rapidjson-973dc9c06dcd3d035ebd039cfb9ea457721ec213/include",
    urls = [
        "https://github.com/Tencent/rapidjson/archive/973dc9c06dcd3d035ebd039cfb9ea457721ec213.zip",
    ],
)

# replxx
# The last release is quite old. So top of tree
http_archive(
    name = "replxx",
    build_file = "replxx.BUILD",
    sha256 = "d8d6eca00efa464089c0240f1a898449938d3f00f53e12e1dd09c55b60e4fbb8",
    strip_prefix = "replxx-5d04501f93a4fb7f0bb8b73b8f614bc986f9e25b",
    urls = [
        "https://github.com/ClickHouse/replxx/archive/5d04501f93a4fb7f0bb8b73b8f614bc986f9e25b.zip",
    ],
)

# verilator
http_archive(
    name = "verilator",
    build_file = "verilator.BUILD",
    patches = ["//external:verilator.patch"],
    sha256 = "1b42e2638080155d1aaa4b26012818a855a448afc718f79cb2b0fe6e610b9de2",
    strip_prefix = "verilator-5.010",
    urls = [
      "https://github.com/verilator/verilator/archive/refs/tags/v5.010.zip"
    ],
)

# anubis
http_archive(
    name = "anubis",
    build_file = "anubis.BUILD",
    strip_prefix = "anubis-93088bd3c05407ccd871e8d5067d024f812aeeaa",
    urls = [
        "https://github.com/masc-ucsc/anubis/archive/93088bd3c05407ccd871e8d5067d024f812aeeaa.zip",
    ],
)

# OpenTimer
http_archive(
    name = "opentimer",
    build_file = "opentimer.BUILD",
    patches = ["//external:opentimer.patch"],
    strip_prefix = "OpenTimer-979a5f33936c8deef846a26df8ef58d1b4a7ca82",
    urls = [
        "https://github.com/masc-ucsc/OpenTimer/archive/979a5f33936c8deef846a26df8ef58d1b4a7ca82.zip",
    ],
)

# mockturtle
http_archive(
    name = "mockturtle",
    build_file = "mockturtle.BUILD",
    sha256 = "562579061863772362856258f2be0984b3c503a497fab8e093bbb10fa1a8ace0",
    strip_prefix = "mockturtle-0.2",
    urls = [
        "https://github.com/lsils/mockturtle/archive/refs/tags/v0.2.zip",
    ],
)

# Protobuf
http_archive(
    name = "rules_proto",
    sha256 = "dc3fb206a2cb3441b485eb1e423165b231235a1ea9b031b4433cf7bc1fa460dd",
    strip_prefix = "rules_proto-5.3.0-21.7",
    urls = [
        "https://github.com/bazelbuild/rules_proto/archive/refs/tags/5.3.0-21.7.tar.gz",
    ],
)
load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies", "rules_proto_toolchains")
rules_proto_dependencies()
rules_proto_toolchains()
# http_archive(
    # name = "rules_proto",
    # sha256 = "e017528fd1c91c5a33f15493e3a398181a9e821a804eb7ff5acdd1d2d6c2b18d",
    # strip_prefix = "rules_proto-4.0.0-3.20.0",
    # urls = [
    #     "https://github.com/bazelbuild/rules_proto/archive/refs/tags/4.0.0-3.20.0.tar.gz",
    # ],
# )
# load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies", "rules_proto_toolchains")
# rules_proto_dependencies()
# rules_proto_toolchains()

# tcmalloc
http_archive(
    name = "com_google_tcmalloc",  # 2021-11-11T17:55:46Z
    sha256 = "7e63ba5b3b9750dcfd744bbdc2f71b2d052e632a393d350dcaacd9ddb005a681",
    strip_prefix = "tcmalloc-4d37e6780a321896f2b00d67c7f854e95599ac6a",
    urls = [
        "https://github.com/google/tcmalloc/archive/4d37e6780a321896f2b00d67c7f854e95599ac6a.zip",
    ],
)

# Fuzzing (required by tcmalloc)
http_archive(
    name = "rules_fuzzing",
    sha256 = "d9002dd3cd6437017f08593124fdd1b13b3473c7b929ceb0e60d317cb9346118",
    strip_prefix = "rules_fuzzing-0.3.2",
    urls = ["https://github.com/bazelbuild/rules_fuzzing/archive/v0.3.2.zip"],
)

load("@rules_fuzzing//fuzzing:repositories.bzl", "rules_fuzzing_dependencies")

rules_fuzzing_dependencies()

load("@rules_fuzzing//fuzzing:init.bzl", "rules_fuzzing_init")

rules_fuzzing_init()

# Perfetto
http_archive(
    name = "com_google_perfetto",
    build_file = "perfetto.BUILD",
    sha256 = "06eec38d02f99d225cdad9444102e77d9da717f8cc55f84a3b212abe94a5fc5a",
    strip_prefix = "perfetto-28.0/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v28.0.tar.gz"],
)

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# Boost (bazel_rules_hdl)
#http_archive(
    #name = "com_github_nelhage_rules_boost",
    #url = "https://github.com/nelhage/rules_boost/archive/96e9b631f104b43a53c21c87b01ac538ad6f3b48.tar.gz",
    #strip_prefix = "rules_boost-96e9b631f104b43a53c21c87b01ac538ad6f3b48",
#)
#load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
#boost_deps()

