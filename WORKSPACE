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
http_archive(
    name = "tree-sitter",
    build_file = "tree-sitter.BUILD",
    sha256 = "1bfd4db2b3a6a4b81058c6a6cfa7fd339e71508c44355c7c8a5034e9b2ad56f1",
    strip_prefix = "tree-sitter-0.25.6",
    urls = [
        "https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v0.25.6.zip",
    ],
)

# nlohmann json
http_archive(
    name = "json",
    build_file = "json.BUILD",
    sha256 = "34660b5e9a407195d55e8da705ed26cc6d175ce5a6b1fb957e701fb4d5b04022",
    strip_prefix = "json-3.12.0",
    urls = [
        "https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.zip",
    ],
)

# HIF
http_archive(
    name = "hif",
    sha256 = "de5988e3ad9b398dd7b9879ddb8b539d02f18d2ffe1f1cad35d07b9696d0b49d",
    strip_prefix = "hif-2102c0507c89c9a37d9e4b7711e3c56994ea6725",
    urls = [
        "https://github.com/masc-ucsc/hif/archive/2102c0507c89c9a37d9e4b7711e3c56994ea6725.zip",
    ],
)

# cryptominisat
http_archive(
    name = "cryptominisat",
    build_file = "cryptominisat.BUILD",
    patches = ["//external:cryptominisat.patch"],
#    sha256 = "e50eb9afc047c04922c2edd4fa6feb108563612b4e9d13154823d2d5bb005a56",
#    strip_prefix = "cryptominisat-5.12.1",
    sha256 = "e50eb9afc047c04922c2edd4fa6feb108563612b4e9d13154823d2d5bb005a56",
    strip_prefix = "cryptominisat-5.12.1",
    urls = [
        "https://github.com/msoos/cryptominisat/archive/refs/tags/5.12.1.zip",
#        "https://github.com/msoos/cryptominisat/archive/refs/tags/5.12.1.zip",
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
    patches = ["//external:replxx.patch"],
    sha256 = "3f97bc606c26778c6e88041afe5912f395308811476c4b3094c514665013bcd8",
    strip_prefix = "replxx-dea4b7c724a2ec41d078383ccf0ebeedfaebaeea",
    urls = [
        "https://github.com/ClickHouse/replxx/archive/dea4b7c724a2ec41d078383ccf0ebeedfaebaeea.zip",
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
    sha256 = "1ee4a2cce57788e3220701e975881d940302cfce45f727f351a42775b7642797",
    strip_prefix = "OpenTimer-bf87721a6bdc7003d785c40f682f237332a9072c",
    urls = [
        "https://github.com/masc-ucsc/OpenTimer/archive/bf87721a6bdc7003d785c40f682f237332a9072c.zip",
    ],
)

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
