
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
new_local_repository(
    name = "yosys",
    path = "third_party/subs/yosys",
    build_file = "third_party/subs/BUILD.yosys",
)
new_local_repository(
    name = "abc",
    path = "third_party/subs/abc",
    build_file = "third_party/subs/BUILD.abc",
)

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

