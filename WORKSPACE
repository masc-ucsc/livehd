# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

workspace(name = "livehd")

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "rules_foreign_cc",
    sha256 = "c2cdcf55ffaf49366725639e45dedd449b8c3fe22b54e31625eb80ce3a240f1e",
    strip_prefix = "rules_foreign_cc-0.1.0",
    url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.1.0.zip",
)

load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

new_git_repository(
    name = "mustache",
    build_file = "BUILD.mustache",
    commit = "a7eebc9bec92676c1931eddfff7637d7e819f2d2",
    remote = "https://github.com/kainjow/Mustache.git",
    shallow_since = "1587271057 -0700",
)

http_archive(
    name = "rules_proto",
    sha256 = "d8992e6eeec276d49f1d4e63cfa05bbed6d4a26cfe6ca63c972827a0d141ea3b",
    strip_prefix = "rules_proto-cfdc2fa31879c0aebe31ce7702b1a9c8a4be02d2",
    urls = [
        "https://github.com/bazelbuild/rules_proto/archive/cfdc2fa31879c0aebe31ce7702b1a9c8a4be02d2.tar.gz",
    ],
)

load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies", "rules_proto_toolchains")

rules_proto_dependencies()

rules_proto_toolchains()

new_git_repository(
    name = "fmt",
    build_file = "BUILD.fmt",
    commit = "7bdf0628b1276379886c7f6dda2cef2b3b374f0b",
    remote = "https://github.com/fmtlib/fmt.git",
    shallow_since = "1606266902 -0800",
)

new_git_repository(
    name = "slang",
    build_file_content = """filegroup(name = "all", srcs = glob(["**"]), visibility = ["//visibility:public"])""",
    commit = "d25fc6d61be527ef19265075cc93525d513a010f",
    remote = "https://github.com/MikePopoloski/slang.git",
    shallow_since = "1623205091 -0400",
)

new_git_repository(
    name = "jsonxxxx",
    build_file = "BUILD.json",
    commit = "68c36963826671d3f3ba157222430109ef932bac",
    remote = "https://github.com/nlohmann/json.git",
)

http_archive(
    name = "json",
    build_file = "BUILD.json",
    sha256 = "6bea5877b1541d353bd77bdfbdb2696333ae5ed8f9e8cc22df657192218cad91",
    urls = ["https://github.com/nlohmann/json/releases/download/v3.9.1/include.zip"],
)

git_repository(
    name = "iassert",
    commit = "5c18eb082262532f621a23023f092f4119a44968",
    remote = "https://github.com/masc-ucsc/iassert.git",
    shallow_since = "1599604886 -0700",
)

new_git_repository(
    name = "cryptominisat",
    build_file = "BUILD.cryptominisat",
    commit = "f8b1da0eed202953912ff8cca10175eab61c0a1d",
    patches = ["//external:patch.cryptominisat"],
    remote = "https://github.com/msoos/cryptominisat.git",
    shallow_since = "1598796721 +0200",
)

new_git_repository(
    name = "boolector",
    build_file = "BUILD.boolector",
    commit = "03d76134f86170ab0767194c339fd080e92ad371",
    patches = ["//external:patch.boolector"],
    remote = "https://github.com/Boolector/boolector",
    shallow_since = "1598469820 -0700",
)

new_git_repository(
    name = "rapidjson",
    build_file = "BUILD.rapidjson",
    commit = "6534506e829a489bda78bc5eac5faa34da0a2c51",
    remote = "https://github.com/Tencent/rapidjson.git",
    shallow_since = "1573466634 +0800",
    strip_prefix = "include",
)

new_git_repository(
    name = "replxx",
    build_file = "BUILD.replxx",
    commit = "d13d26504f97ed2a54bc02dd37d20ef3b0179518",
    remote = "https://github.com/AmokHuginnsson/replxx.git",
    shallow_since = "1622167435 +0200",
)

new_git_repository(
    name = "verilator",
    build_file = "BUILD.verilator",
    commit = "97d89cce35142d1a1f4c08571d436d5a65e34901",
    patches = ["//external:patch.verilator"],
    remote = "http://git.veripool.org/git/verilator",
)

new_git_repository(
    name = "anubis",
    build_file = "BUILD.anubis",
    commit = "93088bd3c05407ccd871e8d5067d024f812aeeaa",
    remote = "https://github.com/masc-ucsc/anubis.git",
    shallow_since = "1585037117 +0100",
)

new_git_repository(
    name = "mockturtle",
    build_file = "BUILD.mockturtle",
    commit = "d1b697361d53b4f137d55a18582b290f54ee86bb",
    remote = "https://github.com/lsils/mockturtle.git",
    shallow_since = "1585037117 +0100",
)

http_archive(
    name = "rules_python",
    sha256 = "b6d46438523a3ec0f3cead544190ee13223a52f6a6765a29eae7b7cc24cc83a0",
    url = "https://github.com/bazelbuild/rules_python/releases/download/0.1.0/rules_python-0.1.0.tar.gz",
)

http_archive(
    name = "rules_hdl",
    sha256 = "1dd1ae619c6bce5d994de487bc43cf4c32f80187352826e66596ed8ec9a9058b",
    strip_prefix = "bazel_rules_hdl-e8670bce1cf0ddc1da8fa97227da30306585ac34",
    url = "https://github.com/masc-ucsc/bazel_rules_hdl/archive/e8670bce1cf0ddc1da8fa97227da30306585ac34.zip",
)

load("@rules_hdl//dependency_support:dependency_support.bzl", "dependency_support")

dependency_support()

load("@rules_hdl//:init.bzl", "init")

init()
