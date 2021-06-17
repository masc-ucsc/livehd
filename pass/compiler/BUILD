# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_compiler",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//pass/cprop:pass_cprop",
        "//pass/gioc:pass_gioc",
        "//pass/bitwidth:pass_bitwidth",
        "//pass/firmap:pass_firmap",
        "//pass/lnast_tolg:pass_lnast_tolg",
        "//pass/common:pass",
        "//inou/graphviz:inou_graphviz",
        "//inou/yosys:yosys2",
    ],
    alwayslink = True,
)

sh_test(
    name = "pyrope_compile",
    srcs = ["tests/pyrope_compile.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
    # tags = [
    #     "fixme",
    #     "manual",
    # ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "pyrope2pyrope_compile",
    srcs = ["tests/pyrope2pyrope_compile.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
   # tags = [
   #     "fixme",
   # ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "firrtl_compile",
    srcs = ["tests/firrtl_compile.sh"],
    data = [
        "//inou/firrtl:firrtl_tests",
        "//main:lgshell",
    ],
    # tags = [
    #     "fixme",
    #     "manual",
    # ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "regression_compile",
    srcs = ["tests/regression_compile.sh"],
    data = [
        "//inou/firrtl:firrtl_tests",
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
    tags = ["fixme"],
    deps = [
        "//inou/yosys:scripts",
    ],
)
