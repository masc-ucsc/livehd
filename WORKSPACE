# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

workspace(name = "livehd")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "rules_foreign_cc",
    sha256 = "30c970bfaeda3485100c62b13093da2be2c70ed99ec8d30f4fac6dd37cb25f34",
    strip_prefix = "rules_foreign_cc-0.6.0",
    url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.6.0.zip",
)

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

# rules_hdl
http_archive(
    name = "rules_python",
    sha256 = "b6d46438523a3ec0f3cead544190ee13223a52f6a6765a29eae7b7cc24cc83a0",
    url = "https://github.com/bazelbuild/rules_python/releases/download/0.1.0/rules_python-0.1.0.tar.gz",
)

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
    sha256 = "c426178bf3fa0888a59c4d88977a9fc754a82039a2adad19239555466fb47a42",
    strip_prefix = "Mustache-a7eebc9bec92676c1931eddfff7637d7e819f2d2",
    urls = [
        "https://github.com/kainjow/Mustache/archive/a7eebc9bec92676c1931eddfff7637d7e819f2d2.zip",
    ],
)

# google benchmark
http_archive(
    name = "com_google_benchmark",
    sha256 = "ef0fab8058be682e382e155eeda1b22e1747fd6606e22e0c1b19f6da91e8b52d",
    strip_prefix = "benchmark-69054ae50e07e9de7cb27f9e2d1d355f74605524",
    urls = ["https://github.com/google/benchmark/archive/69054ae50e07e9de7cb27f9e2d1d355f74605524.zip"],
)

# fmt
http_archive(
    name = "fmt",
    build_file = "fmt.BUILD",
    sha256 = "fccfc86e4aff6c33bff9a1d907b2ba2de2a5a8ab84349be4964a93f8d8c21b62",
    strip_prefix = "fmt-7bdf0628b1276379886c7f6dda2cef2b3b374f0b",
    urls = [
        "https://github.com/fmtlib/fmt/archive/7bdf0628b1276379886c7f6dda2cef2b3b374f0b.zip",
    ],
)

# slang
http_archive(
    name = "slang",
    build_file = "slang.BUILD",
    sha256 = "28263ac3653b8b219ae4e9b9ab0e2b2603bb014c45399db8b85ff2a1ebf3e173",
    strip_prefix = "slang-6f2b66270064f40501754f64883f83bc5e7ca5a6",
    urls = [
        "https://github.com/masc-ucsc/slang/archive/6f2b66270064f40501754f64883f83bc5e7ca5a6.zip",
    ],
)

# tree-sitter-pyrope
http_archive(
    name = "tree-sitter-pyrope",
    build_file = "tree-sitter-pyrope.BUILD",
    sha256 = "463f9f58db02f7b14e83e46f0aedfb8b8495b3de0ef09e9f5878ac84d4b72891",
    strip_prefix = "tree-sitter-pyrope-7a5edc94b750ec7be8dd32eecb4354a7f919f0a7",
    urls = [
        "https://github.com/masc-ucsc/tree-sitter-pyrope/archive/7a5edc94b750ec7be8dd32eecb4354a7f919f0a7.zip",
    ],
)

# tree sitter
http_archive(
    name = "tree-sitter",
    build_file = "tree-sitter.BUILD",
    sha256 = "5dc3a775a41ee9592ffc324410d8cb56e4baa0607dda0fa18832d51f6538e75c",
    strip_prefix = "tree-sitter-2bee7c9b75e3e0163b321502f1f73e2e38943a7e",
    urls = [
        "https://github.com/tree-sitter/tree-sitter/archive/2bee7c9b75e3e0163b321502f1f73e2e38943a7e.zip",
    ],
)

# nlohmann json
http_archive(
    name = "json",
    build_file = "json.BUILD",
    sha256 = "6bea5877b1541d353bd77bdfbdb2696333ae5ed8f9e8cc22df657192218cad91",
    urls = ["https://github.com/nlohmann/json/releases/download/v3.9.1/include.zip"],
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
    strip_prefix = "hif-ad84f39c2717648f17e73649c57ba27a42b018b1",
    urls = [
        "https://github.com/masc-ucsc/hif/archive/ad84f39c2717648f17e73649c57ba27a42b018b1.zip",
    ],
)

# cryptominisat
http_archive(
    name = "cryptominisat",
    build_file = "cryptominisat.BUILD",
    patches = ["//external:cryptominisat.patch"],
    sha256 = "f03e082c94bb20ed672eefab2fc6016192d9fd2eed3a81f40924867c86788494",
    strip_prefix = "cryptominisat-f8b1da0eed202953912ff8cca10175eab61c0a1d",
    urls = [
        "https://github.com/msoos/cryptominisat/archive/f8b1da0eed202953912ff8cca10175eab61c0a1d.zip",
    ],
)

# boolector
http_archive(
    name = "boolector",
    build_file = "boolector.BUILD",
    patches = ["//external:boolector.patch"],
    sha256 = "5339667ebfdc35156a1fc910b84cbcc3fd34028a381d5df9aad790f17e997d03",
    strip_prefix = "boolector-03d76134f86170ab0767194c339fd080e92ad371",
    urls = [
        "https://github.com/Boolector/boolector/archive/03d76134f86170ab0767194c339fd080e92ad371.zip",
    ],
)

# rapidjson
http_archive(
    name = "rapidjson",
    build_file = "rapidjson.BUILD",
    sha256 = "a6b8da8f736b25689eb7fe36dff5d5ce8d491f1e08dd92f0729d6dd6da95e0ac",
    strip_prefix = "rapidjson-6534506e829a489bda78bc5eac5faa34da0a2c51/include",
    urls = [
        "https://github.com/Tencent/rapidjson/archive/6534506e829a489bda78bc5eac5faa34da0a2c51.zip",
    ],
)

# replxx
http_archive(
    name = "replxx",
    build_file = "replxx.BUILD",
    sha256 = "fd09cadbbe91f14da3e8899a9748ce312a73bf70e82e532555fcaeeba3148625",
    strip_prefix = "replxx-d13d26504f97ed2a54bc02dd37d20ef3b0179518",
    urls = [
        "https://github.com/AmokHuginnsson/replxx/archive/d13d26504f97ed2a54bc02dd37d20ef3b0179518.zip",
    ],
)

# verilator
http_archive(
    name = "verilator",
    build_file = "verilator.BUILD",
    patches = ["//external:verilator.patch"],
    strip_prefix = "replxx-97d89cce35142d1a1f4c08571d436d5a65e34901",
    urls = [
        "https://github.com/verilator/verilator/archive/97d89cce35142d1a1f4c08571d436d5a65e34901.zip",
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
    strip_prefix = "OpenTimer-242f283a0371acd2fd94abfeba496463809979fc",
    urls = [
        "https://github.com/OpenTimer/OpenTimer/archive/242f283a0371acd2fd94abfeba496463809979fc.zip",
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
    sha256 = "e017528fd1c91c5a33f15493e3a398181a9e821a804eb7ff5acdd1d2d6c2b18d",
    strip_prefix = "rules_proto-4.0.0-3.20.0",
    urls = [
        "https://github.com/bazelbuild/rules_proto/archive/refs/tags/4.0.0-3.20.0.tar.gz",
    ],
)

load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies", "rules_proto_toolchains")
rules_proto_dependencies()
rules_proto_toolchains()

# tcmalloc
http_archive(
    name = "com_google_tcmalloc",  # 2021-11-11T17:55:46Z
    sha256 = "269eeedfcca5f600d39c343154e1229a1a55b611ec475c1f791a83128bec1024",
    strip_prefix = "tcmalloc-7e4c235ffe5313783b256f1c1add7932fd516b07",
    urls = [
        "https://github.com/google/tcmalloc/archive/7e4c235ffe5313783b256f1c1add7932fd516b07.zip",
    ],
)

# Fuzzing (required by tcmalloc)
http_archive(
    name = "rules_fuzzing",
    sha256 = "a5734cb42b1b69395c57e0bbd32ade394d5c3d6afbfe782b24816a96da24660d",
    strip_prefix = "rules_fuzzing-0.1.1",
    urls = ["https://github.com/bazelbuild/rules_fuzzing/archive/v0.1.1.zip"],
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
