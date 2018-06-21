#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# third_party/subs
#new_local_repository(
#    name = "sparsehash",
#    path = "third_party/subs/sparsehash-c11",
#    build_file = "third_party/subs/BUILD.sparsehash",
#)
#new_local_repository(
    #name = "bm",
    #path = "third_party/subs/BitMagic/src",
    #build_file = "third_party/subs/BUILD.bm",
#)
#new_local_repository(
#    name = "spdlog",
#    path = "third_party/subs/spdlog/include",
#    build_file = "third_party/subs/BUILD.spdlog",
#)
#new_local_repository(
    #name = "yosys",
    #path = "third_party/subs/yosys",
    #build_file = "third_party/subs/BUILD.yosys",
#)
#new_local_repository(
#    name = "abc",
#    path = "third_party/subs/abc",
#    build_file = "third_party/subs/BUILD.abc",
#)

# third_party/fork
load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")
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
    name = "pybind11",
    build_file = "BUILD.pybind11",
    commit = "55dc131944c764ba7e30085b971a9d70531114b3", # Nov 14, 2017
    remote = "https://github.com/pybind/pybind11.git",
    strip_prefix = "include",
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


# Python system includes
new_local_repository(
    name = "python3",
    path = "/usr/include/python3.6m", # use "pkg-config --cflags python3" to get path
    build_file = "third_party/BUILD.python3",
)
# BOOST Libraries dependences
git_repository(
    name = "com_github_nelhage_rules_boost",
    commit = "239ce40e42ab0e3fe7ce84c2e9303ff8a277c41a",
    remote = "https://github.com/nelhage/rules_boost",
)

load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
boost_deps()

git_repository(
    name = "subpar",
    remote = "https://github.com/google/subpar",
    tag = "1.3.0",
)

