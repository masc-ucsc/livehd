#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

workspace(name="livehd")

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
    name = "frozen",
    build_file = "BUILD.frozen",
    commit = "3d2b025ff2509f40424855e3f8640fc2fb6b90b9", # July 1, 2020
    remote = "https://github.com/serge-sans-paille/frozen.git",
)
git_repository( # Open_timer user taskflow
    name = "range-v3",
    commit = "6dd1cb6a03a588031868b6ffb66286e6eaab6714", # July 18, 2020
    remote = "https://github.com/ericniebler/range-v3.git",
)

#new_git_repository( # Open_timer user taskflow
    #name = "taskflow",
    #build_file = "BUILD.taskflow",
    #commit = "ef1e9916529ce52ca2968a20ac4f8accbd18cdf4", # April 29, 2019
    #remote = "https://github.com/cpp-taskflow/cpp-taskflow.git",
#)
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
    commit = "de79978372c1953e295fa262444cb0a28a246c5f", # Sep 24, 2020 a66200ed1d1741150092e89c94f5c25676e9e436", # April 28, 2020 6edca05793197a846bbfb0329e836c87fa5aabb6", # Feb 25, 2020 
    #commit = "a66200ed1d1741150092e89c94f5c25676e9e436", # April 28, 2020 6edca05793197a846bbfb0329e836c87fa5aabb6", # Feb 25, 2020 
    remote = "https://github.com/YosysHQ/yosys.git",
    #strip_prefix = "kernel",
    shallow_since = "1588020530 -0700",
)
new_git_repository(
    name = "mustache",
    build_file = "BUILD.mustache", # relative to external path
    commit = "a7eebc9bec92676c1931eddfff7637d7e819f2d2", # August 10, 2020 "40ddfe9daecc699eca319f1c739b0cfc7e5f3ae5", # April 6 2019
    remote = "https://github.com/kainjow/Mustache.git",
    #strip_prefix = "kernel",
    shallow_since = "1587271057 -0700",
)

http_archive(
    name = "rules_cc",
    urls = ["https://github.com/bazelbuild/rules_cc/archive/262ebec3c2296296526740db4aefce68c80de7fa.zip"],
    strip_prefix = "rules_cc-262ebec3c2296296526740db4aefce68c80de7fa",
    )
git_repository(
    name = "com_google_absl",
    #build_file = "BUILD.abseil", # relative to external path
    #commit = "d0c433455801e1c1fb6f486f0b447e22f946ab52", # August, 16 2020 (fails because Yosys)
    commit = "bf86cfe165ef7d70dfe68f0b8fc0c018bc79a577", # December 16, 2019
    remote = "https://github.com/abseil/abseil-cpp.git",
)

new_git_repository(
    name = "fmt",
    build_file = "BUILD.fmt",
    commit = "cd4af11efc9c622896a3e4cb599fa28668ca3d05", # 7.0.3 August 20 f19b1a521ee8b606dedcadfda69fd10ddf882753", # 7.0.1 June 23, 2020
    remote = "https://github.com/fmtlib/fmt.git",
    #strip_prefix = "include",
    shallow_since = "1596729061 -0700",
)
# Move xxhash.c to xxhash.cpp, fix include inside xxhash.h
# mkdir build; cd build ; cmake ../ ; make ; mv source ../generated/ ; cd ..
# rm -rf source/codegen # LLVM code generation code
# mkpatch --exclude=CMakeFiles ../slang_orig/ .
# s/\.\/slang_orig//g
new_git_repository(
    name = "slang",
    build_file = "BUILD.slang",
    commit = "2a774123444a6152f40639296cf20e90dd7d55b0", # July 23, 2020 # 823fc41d44d53797f0b5ddb1242028cc1fd51f18", #June 12, 2020
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
    commit = "5c18eb082262532f621a23023f092f4119a44968", # September 8, 2020
    remote = "https://github.com/masc-ucsc/iassert.git",
    #strip_prefix = "src",
)
#git_repository(
#    name = "cryptominisat",
#    commit = "d522ab933584ea812429bfb22f752088ed7be599", # August 10, 2019
#    remote = "https://github.com/masc-ucsc/cryptominisat.git",
#    shallow_since = "1565452382 -0700",
#)
new_git_repository(
    name = "cryptominisat",
    build_file = "BUILD.cryptominisat",
    commit = "f8b1da0eed202953912ff8cca10175eab61c0a1d", # September 1, 2020
    remote = "https://github.com/msoos/cryptominisat.git",
    patches = ["//external:patch.cryptominisat"],
)
new_git_repository(
    name = "boolector",
    build_file = "BUILD.boolector",
    commit = "03d76134f86170ab0767194c339fd080e92ad371", # September 1, 2020
    remote = "https://github.com/Boolector/boolector",
    patches = ["//external:patch.boolector"],
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
    shallow_since = "1583869163 +0100",
)
new_git_repository(
    name = "gtest",
    build_file = "BUILD.gtest",
    commit = "adeef192947fbc0f68fa14a6c494c8df32177508", # August 15, 2020 "37f322783175a66c11785d17fc153477b0777753", # October 24, 2019
    remote = "https://github.com/google/googletest",
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
new_git_repository(
    name = "graph",
    build_file = "BUILD.graph",
    commit = "b1e38e1084a0dff6f4eb4ed9a645ed63d3e83dd2", # latest commit as of 7/18/20
    remote = "https://github.com/cbbowen/graph",
)
new_git_repository(
	name = "range-v3",
	build_file = "BUILD.rangev3",
	commit = "4f4beb45c5e56aca4233e4d4c760208e21fff2ec", # specific commit used by graph, made on Jan 11 2019
	remote = "https://github.com/ericniebler/range-v3",
  shallow_since = "1547250373 -0800",
)

# BOOST Libraries dependences
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

git_repository(
    name = "com_github_nelhage_rules_boost",
    commit = "0cc5bf5513c067917b5e083cee22a8dcdf2e0266", # Original "9f9fb8b2f0213989247c9d5c0e814a8451d18d7f",
    remote = "https://github.com/nelhage/rules_boost",
    shallow_since = "1570056263 -0700",
)

load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
boost_deps()

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

#git_repository(
#    name = "rules_graal",
#    commit = "acfe667b44c6a8e78178eb39ad10cc5ba2ee954c",
#    remote = "git://github.com/andyscott/rules_graal",
#)
#
#load("@rules_graal//graal:graal_bindist.bzl", "graal_bindist_repository")
#
#graal_bindist_repository(
#    name = "graal",
#    version = "19.3.1",
#    java_version = "11",
#)

http_archive(
    name = "com_google_protobuf",
    strip_prefix = "protobuf-master",
    urls = ["https://github.com/protocolbuffers/protobuf/archive/master.zip"],
)

load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
protobuf_deps()


