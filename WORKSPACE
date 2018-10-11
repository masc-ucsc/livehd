#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.


load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

new_git_repository(
    name = "OpenTimer",
    build_file = "BUILD.OpenTimer", # relative to external path
    commit = "931b2fbe867e2a5241d86d45fea26a07c1f8b0d6", # Oct 8, 2018
    remote = "https://github.com/OpenTimer/OpenTimer.git",
)
new_git_repository(
    name = "abc",
    build_file = "BUILD.abc", # relative to external path
    commit = "15939511df8ff1ce15f2112cee01d7693234f2a4", # Jun 13, 2018
    remote = "https://github.com/berkeley-abc/abc.git",
    patches = ["//external:patch.abc"],
)
new_git_repository(
    name = "yosys",
    build_file = "BUILD.yosys", # relative to external path
    commit = "57fc8dd58229d309ba56b374223802936444ecd4", # Jun 13, 2018
    remote = "https://github.com/YosysHQ/yosys.git",
    #strip_prefix = "kernel",
)
new_git_repository(
    name = "mustache",
    build_file = "BUILD.mustache", # relative to external path
    commit = "2c37b240f6d9147b4a7639c433fdde2f31b6868f", # Sep 22, 2018
    remote = "https://github.com/kainjow/Mustache.git",
    #strip_prefix = "kernel",
)
new_git_repository(
    name = "spdlog",
    build_file = "BUILD.spdlog",
    commit = "032035e72f73b232e2fa087dc6021a3732c9f6ae", # June 10, 2018
    remote = "https://github.com/gabime/spdlog.git",
    strip_prefix = "include",
)
new_git_repository(
    name = "sparsehash",
    build_file = "BUILD.sparsehash",
    commit = "5ca6de766db32b3fb08a040636423cd3988d2d4f", # Jun 8, 2018
    remote = "https://github.com/sparsehash/sparsehash-c11.git",
)
new_git_repository(
    name = "bm",
    build_file = "BUILD.bm",
    commit = "65b1f6d631473d694e7cedc42e755096db03e8e2", # June 13, 2018
    remote = "https://github.com/tlk00/BitMagic.git",
    strip_prefix = "src",
)
new_git_repository(
    name = "verilator",
    build_file = "BUILD.verilator",
    commit = "97d89cce35142d1a1f4c08571d436d5a65e34901", # October 10, 2018
    remote = "http://git.veripool.org/git/verilator",
    patches = ["//external:patch.verilator"],
    #strip_prefix = "include",
)
new_git_repository(
    name = "rapidjson",
    build_file = "BUILD.rapidjson",
    commit = "663f076c7b44ce96526d1acfda3fa46971c8af31", # October 6, 2018
    remote = "https://github.com/Tencent/rapidjson.git",
    strip_prefix = "include",
)
new_git_repository(
    name = "httplib",
    build_file = "BUILD.httplib",
    commit = "4d7cee81eb106c502738b8a9980422a93dba148a", # Sep 25, 2018
    remote = "https://github.com/yhirose/cpp-httplib.git",
)
new_git_repository(
    name = "replxx",
    build_file = "BUILD.replxx",
    commit = "228038cbca2532a35cf3fb596eda0d8335fab212", # September 15, 2018
    remote = "https://github.com/AmokHuginnsson/replxx.git",
)
new_git_repository(
    name = "gtest",
    build_file = "BUILD.gtest",
    remote = "https://github.com/google/googletest",
    tag = "release-1.8.0",
)
#load(
#    "//tools:externals.bzl",
#    "new_patched_http_archive",
#)
#
#new_patched_http_archive(
#    name = "abc",
#    build_file = "//third_party/fork:BUILD.abc",
#    patch_file = "//third_party/fork:abc.patch",
#    sha256 = "9164cb6044dcb6e430555721e3318d5a8f38871c2da9fd9256665746a69351e0",
#    strip_prefix = "libdivsufsort-2.0.1",
#    type = "tgz",
#    url = "https://codeload.github.com/y-256/libdivsufsort/tar.gz/2.0.1",
#)

# BOOST Libraries dependences
#git_repository(
    #name = "com_github_nelhage_rules_boost",
    #commit = "96ba810e48f4a28b85ee9c922f0b375274a97f98",
    #remote = "https://github.com/nelhage/rules_boost",
#)

#load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
#boost_deps()

#git_repository(
    #name = "subpar",
    #remote = "https://github.com/google/subpar",
    #tag = "1.3.0",
#)

