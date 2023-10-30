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

# bazel_rules_hdl
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
    sha256 = "abfc22e33e3594d0edf8eaddaf4d84a2ffc491ad74b6a7edc6e7a608f690e691",
    strip_prefix = "benchmark-1.8.3",
    urls = ["https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip"],
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
    #sha256 = "cdc885473510ae0ea909b5589367f8da784df8b00325c55c7cbbab3058424120",
    sha256 = "5bf4d5358301fdf3bd100c01b9d4c1fbb2091dc2267fb4fa6d7cd522b3e47179",
    #strip_prefix = "fmt-9.1.0",
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
    sha256 = "eea4b20751fa6394647330518c9b0c46c8248fea984c91a8a8bfe01a8a04567e",
    strip_prefix = "slang-458be618a58aa1896398eccc1ddf75b880afaab6",
    urls = [
        "https://github.com/MikePopoloski/slang/archive/458be618a58aa1896398eccc1ddf75b880afaab6.zip",
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
        "https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v0.20.8.zip",
    ],
)

# nlohmann json
http_archive(
    name = "json",
    build_file = "json.BUILD",
    sha256 = "95651d7d1fcf2e5c3163c3d37df6d6b3e9e5027299e6bd050d157322ceda9ac9",
    strip_prefix = "json-3.11.2",
    urls = [
        "https://github.com/nlohmann/json/archive/refs/tags/v3.11.2.zip",
    ],
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
    strip_prefix = "hif-9fabf0ec6d8c5dc344fbd2114c99b41833463590",
    urls = [
        "https://github.com/masc-ucsc/hif/archive/9fabf0ec6d8c5dc344fbd2114c99b41833463590.zip",
    ],
)

# cryptominisat
http_archive(
    name = "cryptominisat",
    build_file = "cryptominisat.BUILD",
    patches = ["//external:cryptominisat.patch"],
    sha256 = "70f846f00fa01e64682bfcdcbdde217be168c4aebc861aefc96d6853234507f7",
    strip_prefix = "cryptominisat-5.11.11",
    urls = [
        "https://github.com/msoos/cryptominisat/archive/refs/tags/5.11.11.zip",
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
    sha256 = "8ea39c59b37bfd1888b262daa75e70ccd2b60790f9f841d2f44b41df417f75b7",
    strip_prefix = "rapidjson-a95e013b97ca6523f32da23f5095fcc9dd6067e5/include",
    urls = [
        "https://github.com/Tencent/rapidjson/archive/a95e013b97ca6523f32da23f5095fcc9dd6067e5.zip",
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
        "https://github.com/verilator/verilator/archive/refs/tags/v5.010.zip",
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
    sha256 = "cf828f8c43422869124ebe601966a2c2763b7f1d7363dca1b6aeddc84ae91ef3",
    strip_prefix = "OpenTimer-5a6af16c5443e2eedc697e86d3fb88e2b487eaa3",
    urls = [
        "https://github.com/masc-ucsc/OpenTimer/archive/5a6af16c5443e2eedc697e86d3fb88e2b487eaa3.zip",
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

# mimalloc (only seems to work in Linux)
http_archive(
    name = "mimalloc",
    build_file = "mimalloc.BUILD",
    sha256 = "86281c918921c1007945a8a31e5ad6ae9af77e510abfec20d000dd05d15123c7",
    strip_prefix = "mimalloc-2.1.2",
    urls = [
        "https://github.com/microsoft/mimalloc/archive/refs/tags/v2.1.2.zip",
    ],
)

# Perfetto
http_archive(
    name = "com_google_perfetto",
    build_file = "perfetto.BUILD",
    sha256 = "92160b0fbeb8c4992cc0690d832dd923cee1dda466f3364ef4ed26a835e55e40",
    strip_prefix = "perfetto-38.0/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v38.0.tar.gz"],
)

# Boost
http_archive(
    name = "com_github_nelhage_rules_boost",
    strip_prefix = "rules_boost-4ab574f9a84b42b1809978114a4664184716f4bf",
    url = "https://github.com/nelhage/rules_boost/archive/4ab574f9a84b42b1809978114a4664184716f4bf.tar.gz",
)

load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")

boost_deps()
