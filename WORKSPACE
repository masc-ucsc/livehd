#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

#git_repository(
    #name = "bazel_skylib",
    #remote = "https://github.com/bazelbuild/bazel-skylib.git",
    #tag = "0.8.0",  # change this to use a different release
#)

new_git_repository(
    name = "opentimer",
    build_file = "BUILD.opentimer", # relative to external path
    commit = "6552db088d45a94d108b34a5448a5d5da1a30cf7", # April 29
    remote = "https://github.com/OpenTimer/OpenTimer.git",
    #strip_prefix = "ot", OpenTimer uses ot/... so, we have to keep it
    #patches = ["//external:patch.opentimer"],
)
new_git_repository( # Open_timer user taskflow
    name = "taskflow",
    build_file = "BUILD.taskflow",
    commit = "ef1e9916529ce52ca2968a20ac4f8accbd18cdf4", # April 29
    remote = "https://github.com/cpp-taskflow/cpp-taskflow.git",
)
new_git_repository(
    name = "abc",
    build_file = "BUILD.abc", # relative to external path
    commit = "362b2d9d08f4dbc8dfc751b68ddf7bd3f9c4ed54", # April 6 14d985a8c4597bc70765cb889be160b7af5fa128", # Oct 20, 2018
    remote = "https://github.com/berkeley-abc/abc.git",
    patches = ["//external:patch.abc"],
)
new_git_repository(
    name = "yosys",
    build_file = "BUILD.yosys", # relative to external path
    commit = "349c47250a9779bc58634870d2e3facfe95fbff8", #May 28, 2019 dfb242c905ff10bb4038f080aeb74a820e8fbd00", # April 6, 2019 
    remote = "https://github.com/YosysHQ/yosys.git",
    #strip_prefix = "kernel",
)
new_git_repository(
    name = "mustache",
    build_file = "BUILD.mustache", # relative to external path
    commit = "40ddfe9daecc699eca319f1c739b0cfc7e5f3ae5", # April 6 2019 2c37b240f6d9147b4a7639c433fdde2f31b6868f", # Sep 22, 2018
    remote = "https://github.com/kainjow/Mustache.git",
    #strip_prefix = "kernel",
)
git_repository(
    name = "com_google_absl",
    #build_file = "BUILD.abseil", # relative to external path
    commit = "6cc6ac44e065b9e8975fadfd6ccb99cbcf89aac4", # April 6 2019
    remote = "https://github.com/abseil/abseil-cpp.git",
)
new_git_repository(
    name = "fmt",
    build_file = "BUILD.fmt",
    commit = "ab1474ef661a82175de693f79926f38bf11d6815", # April 6, 2019 91acfe685234534b8f8b379031d00cd62240ce3f", # March 20, 2019 "a084495d7e35436304e9ad0df999e06b2faf2a0b", # Nov 29, 2018
    remote = "https://github.com/fmtlib/fmt.git",
    #strip_prefix = "include",
)
new_git_repository(
    name = "iassert",
    build_file = "BUILD.iassert",
    commit = "66f017448fd82a381b6c354fc93d74ce1c872e97", # August 6, 2019
    remote = "https://github.com/masc-ucsc/iassert.git",
    strip_prefix = "src",
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
    commit = "93a8ba2c50c039cfe6b95f9d6fe128603a14e0ed", # March 20, 2019
    remote = "https://github.com/tlk00/BitMagic.git",
    strip_prefix = "src",
)
git_repository(
    name = "cryptominisat",
    #build_file = "BUILD.abseil", # relative to external path
    commit = "d522ab933584ea812429bfb22f752088ed7be599", # August 10, 2019 6ee26181af5c719fa4f6ac1fa1aa3c85d070bcea", # May 12, 2019
    remote = "https://github.com/masc-ucsc/cryptominisat.git",
    shallow_since = "1565452382 -0700",
)
new_git_repository(
    name = "rapidjson",
    build_file = "BUILD.rapidjson",
    #commit = "55c3c241cf55801d8caf0f11af2e98595797801a", # April 6, 2019 663f076c7b44ce96526d1acfda3fa46971c8af31", # October 6, 2018
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
    commit = "8483e5931fb8a07ba8be078c59ee641eff661769", # April 6 2019 b5927aec123351dcf796e1fba8a6a1805d294cbe", # Dec 20, 2018
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
    commit = "5ba69d5cb93779fba14bf438dfdaf589e2b92071", # APril 6 2019
    remote = "https://github.com/google/googletest",
    #tag = "release-1.8.0",
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
new_git_repository(
    name = "mockturtle",
    build_file = "BUILD.mockturtle",
    commit = "28c18ecd78dac3e42876a7e0f044132b9c16f466", # May 8, 2019 1da278ecb239bcdf4e0f6f605fa1e3380a726c5d", # April 6, 2019 
    remote = "https://github.com/lsils/mockturtle.git",
    #patches = ["//external:patch.verilator"],
    #strip_prefix = "include",
)
new_git_repository(
    name = "bison",
    build_file = "BUILD.bison",
    commit = "0d44f83fcc330dd4674cf4493e2a4e18e758e6bc",
    remote = "https://git.savannah.gnu.org/git/bison.git",
    #patches = ["//external:patch.verilator"],
    #strip_prefix = "include",
)

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

# Hermetic even for the toolchain :D
http_archive(
    name = "bazel_toolchains",
    sha256 = "4598bf5a8b4f5ced82c782899438a7ba695165d47b3bf783ce774e89a8c6e617",
    strip_prefix = "bazel-toolchains-0.27.0",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/0.27.0.tar.gz",
        "https://github.com/bazelbuild/bazel-toolchains/archive/0.27.0.tar.gz",
    ],
)

#load("@bazel_toolchains//rules:rbe_repo.bzl", "rbe_autoconfig")

#git_repository(
    #name = "rules_graal",
    #commit = "54c18d0c002670d755c1a709b94336f74375e21d",
    #remote = "git://github.com/andyscott/rules_graal",
#)

#load("@rules_graal//graal:graal_bindist.bzl", "graal_bindist_repository")

#graal_bindist_repository(
    #name = "graal",
    #version = "19.0.0",
#)

