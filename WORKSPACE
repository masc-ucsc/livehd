# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

workspace(name = "livehd")

load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

new_git_repository(
    name = "abc",
    build_file = "abc.BUILD",  # relative to external path
    commit = "64ed5b81a493e285a64057a9db9ebad6867002bb",
    patches = ["//external:abc.patch"],
    remote = "https://github.com/berkeley-abc/abc.git",
    shallow_since = "1554534526 -1000",
)

http_archive(
   name = "at_clifford_yosys2",
   urls = [
     "https://github.com/YosysHQ/yosys/archive/refs/tags/yosys-0.44.zip",
   ],
   sha256 = "39394043d3fee3d5c2a5f1a1e8626f61fee6f11121a5000af3a60a921609f69f",
   strip_prefix = "yosys-yosys-0.44",
   build_file = "yosys.BUILD",
   patches = ["//external:yosys.patch"],
)

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

# fmt
http_archive(
    name = "fmt",
    build_file = "fmt.BUILD",
    sha256 = "3c2e73019178ad72b0614a3124f25de454b9ca3a1afe81d5447b8d3cbdb6d322",
    #sha256 = "7aa4b58e361de10b8e5d7b6c18aebd98be1886ab3efe43e368527a75cd504ae4",
    #strip_prefix = "fmt-11.0.2",
    strip_prefix = "fmt-10.1.1",
    urls = [
        "https://github.com/fmtlib/fmt/archive/refs/tags/10.1.1.zip",
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
http_archive(
    name = "tree-sitter-pyrope",
    build_file = "tree-sitter-pyrope.BUILD",
    sha256 = "ab3fedce4c7ad2c5477c1ff302e1140a9dcb6733f64d676a4b4cf0770080261b",
    strip_prefix = "tree-sitter-pyrope-b5ef0426123c774f01fdae0e77e80c6f0577212c",
    urls = ["https://github.com/masc-ucsc/tree-sitter-pyrope/archive/b5ef0426123c774f01fdae0e77e80c6f0577212c.zip",
    ],
)

# tree sitter
# http_archive(
#     name = "tree-sitter",
#     build_file = "tree-sitter.BUILD",
#     sha256 = "e9f2772b12d4b12a0db5542ce72e8c85a34e397f2c3fd7b3fa08814f71fd35b3",
#     strip_prefix = "tree-sitter-0.23.0",
#     urls = [
#         "https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v0.23.0.zip",
#     ],
# )

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
    sha256 = "b8103f11e679ae8e3dd700f0c41ed4191aa819c215256066a8ff0e531cb77a63",
    strip_prefix = "hif-cf106676965e935aa2e46754e3e17a68a5367781",
    urls = [
        "https://github.com/masc-ucsc/hif/archive/cf106676965e935aa2e46754e3e17a68a5367781.zip",
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
http_archive(
    name = "boolector",
    build_file = "boolector.BUILD",
    patches = ["//external:boolector.patch"],
    sha256 = "de4a91d4d271909aa0e1ca646105579cbdcd7676842f517237fc6238ca39aa79",
    strip_prefix = "boolector-43dae91c1070e5e2633e036ebd75ffb13fe261e1",
    urls = [
        "https://github.com/Boolector/boolector/archive/43dae91c1070e5e2633e036ebd75ffb13fe261e1.zip",
    ],
)

# replxx
http_archive(
    name = "replxx",
    build_file = "replxx.BUILD",
    sha256 = "96a6354d5f5e5afa2c3e7969647e847a7a9b989a78e21272c1915bb0b0edfdf0",
    strip_prefix = "replxx-9f72931df8ce356d92bad6d267590a8b6e099358",
    urls = [
        #"https://github.com/ClickHouse/replxx/archive/711c18e7f4d951255aa8b0851e5a55d5a5fb0ddb.zip",
        "https://github.com/ClickHouse/replxx/archive/9f72931df8ce356d92bad6d267590a8b6e099358.zip",

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
# http_archive(
#     name = "rules_proto_grpc",
#     sha256 = "9ba7299c5eb6ec45b6b9a0ceb9916d0ab96789ac8218269322f0124c0c0d24e2",
#     strip_prefix = "rules_proto_grpc-4.5.0",
#     urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/releases/download/4.5.0/rules_proto_grpc-4.5.0.tar.gz"],
# )
#
# load("@rules_proto_grpc//:repositories.bzl", "rules_proto_grpc_repos", "rules_proto_grpc_toolchains")
#
# rules_proto_grpc_toolchains()
#
# rules_proto_grpc_repos()

# load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies", "rules_proto_toolchains")
#
# rules_proto_dependencies()
#
# rules_proto_toolchains()
# http_archive(
#     name = "rules_proto",
#     sha256 = "fea00227e78467fc8ab6a17f7de26489b7dcb3b773659cf6d9906251e521cfe9",
#     strip_prefix = "rules_proto-aaa54ca64e87699276b6f64e22ce800fae3637b5",
#     urls = [
#         "https://github.com/bazelbuild/rules_proto/archive/aaa54ca64e87699276b6f64e22ce800fae3637b5.zip",
#     ],
# )
# load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies", "rules_proto_toolchains")
# rules_proto_dependencies()
# rules_proto_toolchains()

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

# Perfetto
http_archive(
    name = "com_google_perfetto",
    build_file = "perfetto.BUILD",
    sha256 = "bd78f0165e66026c31c8c39221ed2863697a8bba5cd39b12e4b43d0b7f71626f",
    strip_prefix = "perfetto-40.0/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v40.0.tar.gz"],
)

# Boost
http_archive(
    name = "com_github_nelhage_rules_boost",
    strip_prefix = "rules_boost-8fa193c4e21daaa2d46ff6b9c2b5a2de70b6caa1",
    url = "https://github.com/nelhage/rules_boost/archive/8fa193c4e21daaa2d46ff6b9c2b5a2de70b6caa1.tar.gz",
)

load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")

boost_deps()
