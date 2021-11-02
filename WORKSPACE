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

http_archive(
    name = "mustache",
    build_file = "BUILD.mustache",
    sha256 = "c426178bf3fa0888a59c4d88977a9fc754a82039a2adad19239555466fb47a42",
    strip_prefix = "Mustache-a7eebc9bec92676c1931eddfff7637d7e819f2d2",
    urls = [
        "https://github.com/kainjow/Mustache/archive/a7eebc9bec92676c1931eddfff7637d7e819f2d2.zip",
    ],
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


http_archive(
  name = "com_google_benchmark",
  sha256 = "ef0fab8058be682e382e155eeda1b22e1747fd6606e22e0c1b19f6da91e8b52d",
  urls = ["https://github.com/google/benchmark/archive/69054ae50e07e9de7cb27f9e2d1d355f74605524.zip"],
  strip_prefix = "benchmark-69054ae50e07e9de7cb27f9e2d1d355f74605524",
)

http_archive(
    name = "fmt",
    build_file = "BUILD.fmt",
    sha256 = "fccfc86e4aff6c33bff9a1d907b2ba2de2a5a8ab84349be4964a93f8d8c21b62",
    strip_prefix = "fmt-7bdf0628b1276379886c7f6dda2cef2b3b374f0b",
    urls = [
        "https://github.com/fmtlib/fmt/archive/7bdf0628b1276379886c7f6dda2cef2b3b374f0b.zip",
    ],
)

http_archive(
    name = "slang",
    build_file = "BUILD.slang",
    sha256 = "982fae57bb994486979df7585205eaf566c5aa1266a469fce68286fc06113208",
    strip_prefix = "slang-livehd-1",
    urls = [
        "https://github.com/masc-ucsc/slang/archive/livehd-1.zip",
    ],
)

http_archive(
    name = "tree-sitter-pyrope",
    build_file = "BUILD.tree-sitter-pyrope",
    sha256 = "fcdf4be791297e3a6ef9804e9a5254db6fcc3f675d401d0a0a58e2db4091bf6c",
    strip_prefix = "tree-sitter-pyrope-b543bf668055454c3e5bcd03fde64bfd8b3da984",
    urls = [
        "https://github.com/masc-ucsc/tree-sitter-pyrope/archive/b543bf668055454c3e5bcd03fde64bfd8b3da984.zip",
    ],
)

http_archive(
    name = "tree-sitter",
    build_file = "BUILD.tree-sitter",
    sha256 = "5dc3a775a41ee9592ffc324410d8cb56e4baa0607dda0fa18832d51f6538e75c",
    strip_prefix = "tree-sitter-2bee7c9b75e3e0163b321502f1f73e2e38943a7e",
    urls = [
        "https://github.com/tree-sitter/tree-sitter/archive/2bee7c9b75e3e0163b321502f1f73e2e38943a7e.zip",
    ],
)

http_archive(
    name = "json",
    build_file = "BUILD.json",
    sha256 = "6bea5877b1541d353bd77bdfbdb2696333ae5ed8f9e8cc22df657192218cad91",
    urls = ["https://github.com/nlohmann/json/releases/download/v3.9.1/include.zip"],
)

http_archive(
    name = "iassert",
    sha256 = "c6bf66a76d5a1de57c45dba137c9b51ab3b4f3a31e5de9e3c3496d7d36a128f8",
    strip_prefix = "iassert-5c18eb082262532f621a23023f092f4119a44968",
    urls = [
        "https://github.com/masc-ucsc/iassert/archive/5c18eb082262532f621a23023f092f4119a44968.zip",
    ],
)

http_archive(
    name = "cryptominisat",
    build_file = "BUILD.cryptominisat",
    patches = ["//external:patch.cryptominisat"],
    sha256 = "f03e082c94bb20ed672eefab2fc6016192d9fd2eed3a81f40924867c86788494",
    strip_prefix = "cryptominisat-f8b1da0eed202953912ff8cca10175eab61c0a1d",
    urls = [
        "https://github.com/msoos/cryptominisat/archive/f8b1da0eed202953912ff8cca10175eab61c0a1d.zip",
    ],
)

http_archive(
    name = "boolector",
    build_file = "BUILD.boolector",
    patches = ["//external:patch.boolector"],
    sha256 = "5339667ebfdc35156a1fc910b84cbcc3fd34028a381d5df9aad790f17e997d03",
    strip_prefix = "boolector-03d76134f86170ab0767194c339fd080e92ad371",
    urls = [
        "https://github.com/Boolector/boolector/archive/03d76134f86170ab0767194c339fd080e92ad371.zip",
    ],
)

http_archive(
    name = "rapidjson",
    build_file = "BUILD.rapidjson",
    sha256 = "a6b8da8f736b25689eb7fe36dff5d5ce8d491f1e08dd92f0729d6dd6da95e0ac",
    strip_prefix = "rapidjson-6534506e829a489bda78bc5eac5faa34da0a2c51/include",
    urls = [
        "https://github.com/Tencent/rapidjson/archive/6534506e829a489bda78bc5eac5faa34da0a2c51.zip",
    ],
)

http_archive(
    name = "replxx",
    build_file = "BUILD.replxx",
    sha256 = "fd09cadbbe91f14da3e8899a9748ce312a73bf70e82e532555fcaeeba3148625",
    strip_prefix = "replxx-d13d26504f97ed2a54bc02dd37d20ef3b0179518",
    urls = [
        "https://github.com/AmokHuginnsson/replxx/archive/d13d26504f97ed2a54bc02dd37d20ef3b0179518.zip",
    ],
)

http_archive(
    name = "verilator",
    build_file = "BUILD.verilator",
    patches = ["//external:patch.verilator"],
    strip_prefix = "replxx-97d89cce35142d1a1f4c08571d436d5a65e34901",
    urls = [
        "https://github.com/verilator/verilator/archive/97d89cce35142d1a1f4c08571d436d5a65e34901.zip",
    ],
)

http_archive(
    name = "anubis",
    build_file = "BUILD.anubis",
    strip_prefix = "anubis-93088bd3c05407ccd871e8d5067d024f812aeeaa",
    urls = [
        "https://github.com/masc-ucsc/anubis/archive/93088bd3c05407ccd871e8d5067d024f812aeeaa.zip",
    ],
)

http_archive(
    name = "mockturtle",
    build_file = "BUILD.mockturtle",
    strip_prefix = "mockturtle-d1b697361d53b4f137d55a18582b290f54ee86bb",
    urls = [
        "https://github.com/lsils/mockturtle/archive/d1b697361d53b4f137d55a18582b290f54ee86bb.zip",
    ],
)

http_archive(
    name = "rules_python",
    sha256 = "b6d46438523a3ec0f3cead544190ee13223a52f6a6765a29eae7b7cc24cc83a0",
    url = "https://github.com/bazelbuild/rules_python/releases/download/0.1.0/rules_python-0.1.0.tar.gz",
)

http_archive(
    name = "rules_hdl",
    sha256 = "e233c7a590cc5b69818bb97cf447fd357a37cf050dcb1b27f1c3b9e87813d05d",
    strip_prefix = "bazel_rules_hdl-c2c3bb868aef449a8a1b84ad45987d6216b01e26",
    url = "https://github.com/masc-ucsc/bazel_rules_hdl/archive/c2c3bb868aef449a8a1b84ad45987d6216b01e26.zip",
)

load("@rules_hdl//dependency_support:dependency_support.bzl", "dependency_support")

dependency_support()

load("@rules_hdl//:init.bzl", "init")

init()
