# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

workspace(name = "livehd")

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "rules_foreign_cc",
    sha256 = "c2cdcf55ffaf49366725639e45dedd449b8c3fe22b54e31625eb80ce3a240f1e",
    strip_prefix = "rules_foreign_cc-0.1.0",
    url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.1.0.zip",
)

load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies(register_preinstalled_tools = False)
#load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")
#bazel_skylib_workspace()
#load("//deps:repositories.bzl", "repositories")
#repositories()

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
#new_git_repository( # Open_timer user taskflow
#    name = "frozen",
#    build_file = "BUILD.frozen",
#    commit = "3d2b025ff2509f40424855e3f8640fc2fb6b90b9", # July 1, 2020
#    remote = "https://github.com/serge-sans-paille/frozen.git",
#)

#new_git_repository( # Open_timer user taskflow
#name = "taskflow",
#build_file = "BUILD.taskflow",
#commit = "ef1e9916529ce52ca2968a20ac4f8accbd18cdf4", # April 29, 2019
#remote = "https://github.com/cpp-taskflow/cpp-taskflow.git",
#)
new_git_repository(
    name = "abc",
    build_file = "BUILD.abc",  # relative to external path
    commit = "362b2d9d08f4dbc8dfc751b68ddf7bd3f9c4ed54",  # April 6 2019
    patches = ["//external:patch.abc"],
    remote = "https://github.com/berkeley-abc/abc.git",
    shallow_since = "1554534526 -1000",
)

new_git_repository(
    name = "yosys",
    build_file = "BUILD.yosys",  # relative to external path
    commit = "de79978372c1953e295fa262444cb0a28a246c5f",  # Sep 24, 2020
    patches = ["//external:patch.yosys"],
    remote = "https://github.com/YosysHQ/yosys.git",
    shallow_since = "1600877724 -0700",
)

new_git_repository(
    name = "mustache",
    build_file = "BUILD.mustache",  # relative to external path
    commit = "a7eebc9bec92676c1931eddfff7637d7e819f2d2",  # August 10, 2020 "40ddfe9daecc699eca319f1c739b0cfc7e5f3ae5", # April 6 2019
    remote = "https://github.com/kainjow/Mustache.git",
    #strip_prefix = "kernel",
    shallow_since = "1587271057 -0700",
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

# abseil-cpp
http_archive(
    name = "com_google_absl",
    sha256 = "8400c511d64eb4d26f92c5ec72535ebd0f843067515244e8b50817b0786427f9",
    strip_prefix = "abseil-cpp-c512f118dde6ffd51cb7d8ac8804bbaf4d266c3a",
    urls = ["https://github.com/abseil/abseil-cpp/archive/c512f118dde6ffd51cb7d8ac8804bbaf4d266c3a.zip"],
)

# Google Test
http_archive(
    name = "com_google_googletest",
    sha256 = "7c7709af5d0c3c2514674261f9fc321b3f1099a2c57f13d0e56187d193c07e81",
    strip_prefix = "googletest-10b1902d893ea8cc43c69541d70868f91af3646b",
    urls = ["https://github.com/google/googletest/archive/10b1902d893ea8cc43c69541d70868f91af3646b.zip"],
)

# C++ rules for Bazel.
http_archive(
    name = "rules_cc",
    sha256 = "954b7a3efc8752da957ae193a13b9133da227bdacf5ceb111f2e11264f7e8c95",
    strip_prefix = "rules_cc-9e10b8a6db775b1ecd358d8ddd3dab379a2c29a5",
    urls = ["https://github.com/bazelbuild/rules_cc/archive/9e10b8a6db775b1ecd358d8ddd3dab379a2c29a5.zip"],
)

#git_repository(
#name = "com_google_absl",
#commit = "e96d49687d9c078f2d47356b6723c3b5715493f7", # Nov, 7 2020
#remote = "https://github.com/abseil/abseil-cpp.git",
#shallow_since = "1604603876 -0500",
#)

git_repository(
    name = "com_google_xls",
    commit = "43cc00719bbf106828aebf57d4b435f7971d5bf5",  # Nov 4, 2020
    remote = "https://github.com/google/xls.git",
)

http_archive(
    name = "tk_tcl_tcl",
    build_file = "BUILD.tk_tcl_tcl",
    sha256 = "5196dbf6638e3df8d5c87b5815c8c2b758496eb6f0e41446596c9a4e638d87ed",
    strip_prefix = "tcl8.6.10",
    urls = [
        "https://prdownloads.sourceforge.net/tcl/tcl8.6.10-src.tar.gz",
    ],
)

new_git_repository(
    name = "fmt",
    build_file = "BUILD.fmt",
    commit = "7bdf0628b1276379886c7f6dda2cef2b3b374f0b",  # 7.1.3 Nov 2020, "cd4af11efc9c622896a3e4cb599fa28668ca3d05", # 7.0.3 August 20 f19b1a521ee8b606dedcadfda69fd10ddf882753", # 7.0.1 June 23, 2020
    remote = "https://github.com/fmtlib/fmt.git",
    #strip_prefix = "include",
    shallow_since = "1606266902 -0800",
)

new_git_repository(
    name = "slang",
    build_file_content = """filegroup(name = "all", srcs = glob(["**"]), visibility = ["//visibility:public"])""",
    commit = "f43d9eedcb2b6e56251d05ad0650118b767ae06b",  # March 30, # eb23621fc9afeb4a04af524ab31abb4ea622b0bf",
    remote = "https://github.com/MikePopoloski/slang.git",
    shallow_since = "1616857210 -0400",
)

new_git_repository(
    name = "jsonxxxx",
    build_file = "BUILD.json",
    commit = "68c36963826671d3f3ba157222430109ef932bac",  # Dec 31, 2020 # d187488e0db0533bdd7c53ec0c687ca1745b8b9e", # Obtober 3, 2019
    remote = "https://github.com/nlohmann/json.git",
    #strip_prefix = "include",
)

http_archive(
    name = "json",
    build_file = "BUILD.json",
    sha256 = "6bea5877b1541d353bd77bdfbdb2696333ae5ed8f9e8cc22df657192218cad91",
    urls = ["https://github.com/nlohmann/json/releases/download/v3.9.1/include.zip"],
)

git_repository(
    name = "iassert",
    #build_file = "BUILD.iassert",
    commit = "5c18eb082262532f621a23023f092f4119a44968",  # September 8, 2020
    remote = "https://github.com/masc-ucsc/iassert.git",
    shallow_since = "1599604886 -0700",
    #strip_prefix = "src",
)

#git_repository(
#    name = "cryptominisat",
#    commit = "d522ab933584ea812429bfb22f752088ed7be599", # August 10, 2019
#    remote = "https://github.com/masc-ucsc/cryptominisat.git",
#    shallow_since = "1598796721 +0200",
#)
new_git_repository(
    name = "cryptominisat",
    build_file = "BUILD.cryptominisat",
    commit = "f8b1da0eed202953912ff8cca10175eab61c0a1d",  # September 1, 2020
    patches = ["//external:patch.cryptominisat"],
    remote = "https://github.com/msoos/cryptominisat.git",
    shallow_since = "1598796721 +0200",
)

new_git_repository(
    name = "boolector",
    build_file = "BUILD.boolector",
    commit = "03d76134f86170ab0767194c339fd080e92ad371",  # September 1, 2020
    patches = ["//external:patch.boolector"],
    remote = "https://github.com/Boolector/boolector",
    shallow_since = "1598469820 -0700",
)

new_git_repository(
    name = "rapidjson",
    build_file = "BUILD.rapidjson",
    commit = "6534506e829a489bda78bc5eac5faa34da0a2c51",  # Nov 23, 2019
    remote = "https://github.com/Tencent/rapidjson.git",
    shallow_since = "1573466634 +0800",
    strip_prefix = "include",
)

#new_git_repository(
#    name = "httplib",
#    build_file = "BUILD.httplib",
#    commit = "e4fd9f19cab6eeaf6489ebb129178b3407e76624", # October 24, 2019
#    remote = "https://github.com/yhirose/cpp-httplib.git",
#    shallow_since = "1571833695 -0400",
#)
new_git_repository(
    name = "replxx",
    build_file = "BUILD.replxx",
    commit = "c634cde996610f4d3330e13c0c9e16bf1034382b",  # March 23, 2020
    remote = "https://github.com/AmokHuginnsson/replxx.git",
    shallow_since = "1583869163 +0100",
)

#new_git_repository(
#    name = "gtest",
#    build_file = "BUILD.gtest",
#    commit = "adeef192947fbc0f68fa14a6c494c8df32177508", # August 15, 2020 "37f322783175a66c11785d17fc153477b0777753", # October 24, 2019
#    remote = "https://github.com/google/googletest",
#    shallow_since = "1597389384 -0400",
#)
new_git_repository(
    name = "verilator",
    build_file = "BUILD.verilator",
    commit = "97d89cce35142d1a1f4c08571d436d5a65e34901",  # October 10, 2018
    patches = ["//external:patch.verilator"],
    remote = "http://git.veripool.org/git/verilator",
    #strip_prefix = "include",
)

new_git_repository(
    name = "anubis",
    build_file = "BUILD.anubis",
    commit = "93088bd3c05407ccd871e8d5067d024f812aeeaa",  # November 06, 2018
    remote = "https://github.com/masc-ucsc/anubis.git",
    shallow_since = "1585037117 +0100",
    #patches = ["//external:patch.verilator"],
    #strip_prefix = "include",
)

new_git_repository(
    name = "mockturtle",
    build_file = "BUILD.mockturtle",
    commit = "d1b697361d53b4f137d55a18582b290f54ee86bb",  # March 20, 2020 19cb4376889a5d91ee947fcbdd3da7a808662a80", # Oct 16 2019
    remote = "https://github.com/lsils/mockturtle.git",
    shallow_since = "1585037117 +0100",
    # patches = ["//external:patch.mockturtle"],
    #strip_prefix = "include",
)

http_archive(
    name = "rules_m4",
    sha256 = "c67fa9891bb19e9e6c1050003ba648d35383b8cb3c9572f397ad24040fb7f0eb",
    urls = ["https://github.com/jmillikin/rules_m4/releases/download/v0.2/rules_m4-v0.2.tar.xz"],
)

load("@rules_m4//m4:m4.bzl", "m4_register_toolchains")

m4_register_toolchains()

http_archive(
    name = "rules_flex",
    sha256 = "f1685512937c2e33a7ebc4d5c6cf38ed282c2ce3b7a9c7c0b542db7e5db59d52",
    urls = ["https://github.com/jmillikin/rules_flex/releases/download/v0.2/rules_flex-v0.2.tar.xz"],
)

load("@rules_flex//flex:flex.bzl", "flex_register_toolchains")

flex_register_toolchains()

http_archive(
    name = "rules_bison",
    sha256 = "6ee9b396f450ca9753c3283944f9a6015b61227f8386893fb59d593455141481",
    urls = ["https://github.com/jmillikin/rules_bison/releases/download/v0.2/rules_bison-v0.2.tar.xz"],
)

load("@rules_bison//bison:bison.bzl", "bison_register_toolchains")

bison_register_toolchains()

#new_git_repository(
#    name = "graph",
#    build_file = "BUILD.graph",
#    commit = "b1e38e1084a0dff6f4eb4ed9a645ed63d3e83dd2", # latest commit Jan 21, 2019
#    remote = "https://github.com/cbbowen/graph",
#    shallow_since = "1548127539 -0800",
#)
#new_git_repository(
#	name = "range-v3",
#	build_file = "BUILD.rangev3",
#	commit = "f013aef2ae81f3661a560e7922a968665bedebff", # 4f4beb45c5e56aca4233e4d4c760208e21fff2ec", # specific commit used by graph, made on Jan 11 2019
#	remote = "https://github.com/ericniebler/range-v3",
#  shallow_since = "1547144220 -0800",
#)

git_repository(
    name = "com_github_nelhage_rules_boost",
    commit = "1e3a69bf2d5cd10c34b74f066054cd335d033d71",  # Nov 7, 2020
    remote = "https://github.com/nelhage/rules_boost",
    shallow_since = "1591047380 -0700",
)

load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")

boost_deps()

# Hermetic even for the toolchain :D
http_archive(
    name = "bazel_toolchains",
    sha256 = "882fecfc88d3dc528f5c5681d95d730e213e39099abff2e637688a91a9619395",
    strip_prefix = "bazel-toolchains-3.4.0",
    urls = [
        "https://github.com/bazelbuild/bazel-toolchains/releases/download/3.4.0/bazel-toolchains-3.4.0.tar.gz",
        "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/releases/download/3.4.0/bazel-toolchains-3.4.0.tar.gz",
    ],
)

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

# Is this used?  Bazel pulls in its own versions of protobuf if required...
http_archive(
    name = "com_google_protobuf",
    #repo_mapping = {"@zlib": "@net_zlib"},
    sha256 = "1c744a6a1f2c901e68c5521bc275e22bdc66256eeb605c2781923365b7087e5f",
    strip_prefix = "protobuf-3.13.0",
    urls = [
        "https://mirror.bazel.build/github.com/protocolbuffers/protobuf/archive/v3.13.0.zip",
        "https://github.com/protocolbuffers/protobuf/archive/v3.13.0.zip",
    ],
)
