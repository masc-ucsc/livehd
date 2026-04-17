# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_shell//shell:sh_library.bzl", "sh_library")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "lgyosys",
    srcs = glob(
        ["*.cpp"],
        exclude = [
            "*test*.cpp",
            "yosys_driver.cpp",
        ],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS + ["-Wno-shadow"],
    visibility = ["//visibility:public"],
    deps = [
        "//pass/common:pass",
        "@at_clifford_yosys2//:kernel",
        "@at_clifford_yosys2//:version",
        "@mustache//:headers",
        "@yosys_slang//:slang.so"
    ],
    alwayslink = True,
)

sh_library(
    name = "scripts",
    srcs = [
        "lgcheck",
        ":inou_yosys_scripts",
        ":yosys2",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "inou_yosys_scripts",
    srcs = glob([
        "inou_yosys_*.ys",
    ]),
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "yosys2",
    srcs = [
        "yosys_driver.cpp",
    ],
    copts = COPTS + ["-Wno-shadow"],
    features = ["-use_header_modules"],
    visibility = ["//visibility:public"],
    linkopts = ["-lz", "-rdynamic"],
    deps = [
        ":lgyosys",
    ],
)

VERILOG_TESTS = glob(
    [
        "tests/*.v",
    ],
    exclude = [
        "tests/fixme*.v",
        "tests/long*.v",
        "tests/nocheck*.v",
    ],
)

filegroup(
    name = "verilog_tests",
    srcs = VERILOG_TESTS,
    visibility = ["//visibility:public"],
)

sh_test(
    name = "lgshell_test.sh",
    srcs = ["lgshell_test.sh"],
    data = [
        ":verilog_tests",
        "//main:lgshell",
    ],
    tags = ["fixme"],
    deps = [
        "//inou/yosys:scripts",
    ],
)

_YOSYS_TEST_PREFIX = "tests/"
_YOSYS_TEST_SUFFIX = ".v"

YOSYS_COMPILE_TESTS = [
    (t[len(_YOSYS_TEST_PREFIX):-len(_YOSYS_TEST_SUFFIX)], t)
    for t in VERILOG_TESTS
]

[sh_test(
    name = "yosys_compile-%s" % t[0],
    srcs = ["tests/yosys_compile.sh"],
    args = ["$(location %s)" % t[1]],
    data = [
        t[1],
        "//main:lgshell",
    ],
    tags = [
    ],
    deps = [
        ":scripts",
    ],
) for t in YOSYS_COMPILE_TESTS]

VERILOG_LONG_TESTS = glob(
    [
        "tests/long*.v",
    ],
)

[sh_test(
    name = "verilog.sh-%s" % t,
    srcs = ["verilog.sh"],
    args = ["$(location %s)" % t],
    data = [
        "%s" % t,
        "//main:lgshell",
    ],
    tags = [
        "fixme",
        "manual",
        "verilog.sh-long",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
) for t in VERILOG_LONG_TESTS]

exports_files(VERILOG_TESTS)
