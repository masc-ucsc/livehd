
# third_party/subs
new_local_repository(
    name = "sparsehash",
    path = "third_party/subs/sparsehash-c11",
    build_file = "third_party/subs/BUILD.sparsehash",
)
new_local_repository(
    name = "bm",
    path = "third_party/subs/BitMagic/src",
    build_file = "third_party/subs/BUILD.bm",
)
new_local_repository(
    name = "pybind11",
    path = "third_party/subs/pybind11/include",
    build_file = "third_party/subs/BUILD.pybind11",
)
new_local_repository(
    name = "spdlog",
    path = "third_party/subs/spdlog/include",
    build_file = "third_party/subs/BUILD.spdlog",
)
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
    commit = "15939511df8ff1ce15f2112cee01d7693234f2a4",
    remote = "https://github.com/berkeley-abc/abc.git",
    patches = ["//external:patch.abc"],
)
new_git_repository(
    name = "yosys",
    build_file = "BUILD.yosys", # relative to external path
    commit = "57fc8dd58229d309ba56b374223802936444ecd4",
    remote = "https://github.com/YosysHQ/yosys.git",
    #strip_prefix = "kernel",
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

