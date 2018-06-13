
new_local_repository(
    name = "sparsehash",
    path = "subs/sparsehash-c11",
    build_file = "subs/BUILD.sparsehash",
)
new_local_repository(
    name = "bm",
    path = "subs/BitMagic/src",
    build_file = "subs/BUILD.bm",
)
new_local_repository(
    name = "pybind11",
    path = "subs/pybind11/include",
    build_file = "subs/BUILD.pybind11",
)
new_local_repository(
    name = "python3",
    path = "/usr/include/python3.6m", # use "pkg-config --cflags python3" to get path
    build_file = "subs/BUILD.python3",
)
new_local_repository(
    name = "spdlog",
    path = "subs/spdlog/include",
    build_file = "subs/BUILD.spdlog",
)
#new_local_repository(
#    name = "ffi",
#    path = "subs/ffi",
#    build_file = "subs/BUILD.ffi",
#)
new_local_repository(
    name = "yosys",
    path = "subs/yosys",
    build_file = "subs/BUILD.yosys",
)
new_local_repository(
    name = "abc",
    path = "subs/abc",
    build_file = "subs/BUILD.abc",
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

