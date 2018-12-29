#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

#git_repository(
    #name = "bazel_skylib",
    #remote = "https://github.com/bazelbuild/bazel-skylib.git",
    #tag = "0.6.0",  # change this to use a different release
#)

new_git_repository(
    name = "Open_timer",
    build_file = "BUILD.Open_timer", # relative to external path
    commit = "4040d90a471a76998b3985de7827df413a5e8652", # Dec 8, 2018
    remote = "https://github.com/OpenTimer/OpenTimer.git",
    #strip_prefix = "ot", OpenTimer uses ot/... so, we have to keep it
    patches = ["//external:patch.Open_timer"],
)
new_git_repository( # Open_timer user taskflow
    name = "taskflow",
    build_file = "BUILD.taskflow",
    commit = "c8a12caed111977c92497c6b5338aacf29e715f9", # Dec 20, 2018
    remote = "https://github.com/cpp-taskflow/cpp-taskflow.git",
)
new_git_repository(
    name = "abc",
    build_file = "BUILD.abc", # relative to external path
    commit = "14d985a8c4597bc70765cb889be160b7af5fa128", # Oct 20, 2018
    remote = "https://github.com/berkeley-abc/abc.git",
    patches = ["//external:patch.abc"],
)
new_git_repository(
    name = "yosys",
    build_file = "BUILD.yosys", # relative to external path
    commit = "93d44bb9a613b46a80642b8ce71295db18fadbc5", # Dec 20, 2018
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
git_repository(
    name = "com_google_absl",
    #build_file = "BUILD.abseil", # relative to external path
    commit = "7ffbe09f3d85504bd018783bbe1e2c12992fe47c", # Dec 27, 2018
    remote = "https://github.com/abseil/abseil-cpp.git",
)
new_git_repository(
    name = "fmt",
    build_file = "BUILD.fmt",
    commit = "a084495d7e35436304e9ad0df999e06b2faf2a0b", # Nov 29, 2018
    remote = "https://github.com/fmtlib/fmt.git",
    #strip_prefix = "include",
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
    commit = "28466673dac932b5b14bf866b9b85a018c7d764b", # Dec 23, 2018
    remote = "https://github.com/tlk00/BitMagic.git",
    strip_prefix = "src",
)
new_git_repository(
    name = "cryptominisat",
    build_file = "BUILD.cryptominisat",
    commit = "4c26a766aab2a452aed9a8fe6f28f4171bcb8690", # October 10, 2018
    remote = "https://github.com/msoos/cryptominisat.git",
    patches = ["//external:patch.cryptominisat"],
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
    name = "yas",
    build_file = "BUILD.yas",
    commit = "f705a06735b87fd97e4f785c31c5791a907f155a", # Dec 21, 2018
    remote = "https://github.com/niXman/yas.git",
    strip_prefix = "include",
)
new_git_repository(
    name = "httplib",
    build_file = "BUILD.httplib",
    commit = "b5927aec123351dcf796e1fba8a6a1805d294cbe", # Dec 20, 2018
    remote = "https://github.com/yhirose/cpp-httplib.git",
)
new_git_repository(
    name = "replxx",
    build_file = "BUILD.replxx",
    commit = "04aa0ec326be427ff351bf0daafbd5ff5933968e", # Dec 10, 2018 (NEXT requires patch)
    remote = "https://github.com/AmokHuginnsson/replxx.git",
)
new_git_repository(
    name = "gtest",
    build_file = "BUILD.gtest",
    remote = "https://github.com/google/googletest",
    tag = "release-1.8.0",
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
    name = "anubis",
    build_file = "BUILD.anubis",
    commit = "93088bd3c05407ccd871e8d5067d024f812aeeaa", # November 06, 2018
    remote = "https://github.com/masc-ucsc/anubis.git"
    #patches = ["//external:patch.verilator"],
    #strip_prefix = "include",
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

