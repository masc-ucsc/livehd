#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

workspace(name="lgraph")

load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")


#git_repository(
    #name = "bazel_skylib",
    #remote = "https://github.com/bazelbuild/bazel-skylib.git",
    #tag = "0.8.0",  # change this to use a different release
#)

#new_git_repository(
    #name = "opentimer",
    #build_file = "BUILD.opentimer", # relative to external path
    #commit = "4f389dc878405340a9bc283ce6fbb4cb133c3566", # April 10, 2020 62f809407ec3a8ac7e6d4f67e7410b92cfc2dbae", # May 18
    #remote = "https://github.com/OpenTimer/OpenTimer.git",
    ##strip_prefix = "ot", OpenTimer uses ot/... so, we have to keep it
    #patches = ["//external:patch.opentimer"],  # For generated ot/config.hpp
#)
new_git_repository( # Open_timer user taskflow
    name = "taskflow",
    build_file = "BUILD.taskflow",
    commit = "ef1e9916529ce52ca2968a20ac4f8accbd18cdf4", # April 29
    remote = "https://github.com/cpp-taskflow/cpp-taskflow.git",
)
new_git_repository(
    name = "abc",
    build_file = "BUILD.abc", # relative to external path
    commit = "362b2d9d08f4dbc8dfc751b68ddf7bd3f9c4ed54", # April 6 2019
    remote = "https://github.com/berkeley-abc/abc.git",
    patches = ["//external:patch.abc"],
)
new_git_repository(
    name = "yosys",
    build_file = "BUILD.yosys", # relative to external path
    commit = "6edca05793197a846bbfb0329e836c87fa5aabb6", # Feb 25, 2020 8a4c6e6563eea979dc96cada14abb08d63a8e3d1", #Aug 26, 2019
    remote = "https://github.com/YosysHQ/yosys.git",
    #strip_prefix = "kernel",
)
new_git_repository(
    name = "mustache",
    build_file = "BUILD.mustache", # relative to external path
    commit = "40ddfe9daecc699eca319f1c739b0cfc7e5f3ae5", # April 6 2019
    remote = "https://github.com/kainjow/Mustache.git",
    #strip_prefix = "kernel",
)
# Needed for bazel abseil package
http_archive(
    name = "rules_cc",
    sha256 = "9a446e9dd9c1bb180c86977a8dc1e9e659550ae732ae58bd2e8fd51e15b2c91d",
    strip_prefix = "rules_cc-262ebec3c2296296526740db4aefce68c80de7fa",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/rules_cc/archive/262ebec3c2296296526740db4aefce68c80de7fa.zip",
        "https://github.com/bazelbuild/rules_cc/archive/262ebec3c2296296526740db4aefce68c80de7fa.zip",
    ],
)
git_repository(
    name = "com_google_absl",
    #build_file = "BUILD.abseil", # relative to external path
    commit = "bf86cfe165ef7d70dfe68f0b8fc0c018bc79a577", # December 16, 2019 e9f9000c7c80993cb589d011616b7a8016e42f4a", # October 11, 2019 a0d1e098c2f99694fa399b175a7ccf920762030e"
    remote = "https://github.com/abseil/abseil-cpp.git",
)
new_git_repository(
    name = "fmt",
    build_file = "BUILD.fmt",
    commit = "f94b7364b9409f05207c3af3fa4666730e11a854", # 6.1.2.0 APril 13, 2020 7512a55aa3ae309587ca89668ef9ec4074a51a1f", # 6.0.0 October 12, 2019
    remote = "https://github.com/fmtlib/fmt.git",
    #strip_prefix = "include",
)
# Move xxhash.c to xxhash.cpp, fix include inside xxhash.h
# mkdir build; cd build ; cmake ../ ; make ; mv source ../generated/ ; cd ..
# rm -rf source/codegen # LLVM code generation code
# mkpatch --exclude=CMakeFiles ../slang_orig/ .
# s/\.\/slang_orig//g
new_git_repository(
    name = "slang",
    build_file = "BUILD.slang",
    commit = "f525b308e8c1c147639e91889fc8d801bc45169e", #April 12, 2020 0e2381c9b408cef18950f928e5c411ed58c54eb6", # Nov 23, 2019
    remote = "https://github.com/MikePopoloski/slang.git",
    patches = ["//external:patch.slang"],
)
new_git_repository(
    name = "json",
    build_file = "BUILD.json",
    commit = "d187488e0db0533bdd7c53ec0c687ca1745b8b9e", # Obtober 3, 2019
    remote = "https://github.com/nlohmann/json.git",
    #strip_prefix = "include",
)
git_repository(
    name = "iassert",
    #build_file = "BUILD.iassert",
    commit = "5bea9d5d1a9605ec52ec2085bd86a627cff643ab", # October 1st, 2019
    remote = "https://github.com/masc-ucsc/iassert.git",
    #strip_prefix = "src",
)
git_repository(
    name = "cryptominisat",
    #build_file = "BUILD.abseil", # relative to external path
    commit = "d522ab933584ea812429bfb22f752088ed7be599", # August 10, 2019
    remote = "https://github.com/masc-ucsc/cryptominisat.git",
    shallow_since = "1565452382 -0700",
)
new_git_repository(
    name = "rapidjson",
    build_file = "BUILD.rapidjson",
    commit = "6534506e829a489bda78bc5eac5faa34da0a2c51", # Nov 23, 2019
    remote = "https://github.com/Tencent/rapidjson.git",
    strip_prefix = "include",
)
new_git_repository(
    name = "httplib",
    build_file = "BUILD.httplib",
    commit = "e4fd9f19cab6eeaf6489ebb129178b3407e76624", # October 24, 2019
    remote = "https://github.com/yhirose/cpp-httplib.git",
)
new_git_repository(
    name = "replxx",
    build_file = "BUILD.replxx",
    commit = "c634cde996610f4d3330e13c0c9e16bf1034382b", # March 23, 2020
    remote = "https://github.com/AmokHuginnsson/replxx.git",
)
new_git_repository(
    name = "gtest",
    build_file = "BUILD.gtest",
    commit = "37f322783175a66c11785d17fc153477b0777753", # October 24, 2019
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
    commit = "d1b697361d53b4f137d55a18582b290f54ee86bb", # March 20, 2020 19cb4376889a5d91ee947fcbdd3da7a808662a80", # Oct 16 2019
    remote = "https://github.com/lsils/mockturtle.git",
    # patches = ["//external:patch.mockturtle"],
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
#http_archive(
    #name = "bazel_toolchains",
    #sha256 = "239a1a673861eabf988e9804f45da3b94da28d1aff05c373b013193c315d9d9e",
    #strip_prefix = "bazel-toolchains-3.0.1",
    #urls = [
        #"https://github.com/bazelbuild/bazel-toolchains/releases/download/3.0.1/bazel-toolchains-3.0.1.tar.gz",
        #"https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/releases/download/3.0.1/bazel-toolchains-3.0.1.tar.gz",
    #],
#)

#load("@bazel_toolchains//rules:rbe_repo.bzl", "rbe_autoconfig")

git_repository(
    name = "rules_graal",
    commit = "acfe667b44c6a8e78178eb39ad10cc5ba2ee954c",
    remote = "git://github.com/andyscott/rules_graal",
)

load("@rules_graal//graal:graal_bindist.bzl", "graal_bindist_repository")

graal_bindist_repository(
    name = "graal",
    version = "19.3.1",
    java_version = "11",
)

http_archive(
    name = "com_google_protobuf",
    strip_prefix = "protobuf-master",
    urls = ["https://github.com/protocolbuffers/protobuf/archive/master.zip"],
)

load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
protobuf_deps()
