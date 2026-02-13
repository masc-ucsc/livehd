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
        "*fixme*",
        "*long*",
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

[sh_test(
    name = "yosys_compile-%s" % t,
    srcs = ["tests/yosys_compile.sh"],
    args = ["$(location //inou/yosys:tests/%s.v)" % t],
    data = [
        "//inou/yosys:tests/%s.v" % t,
        "//main:lgshell",
    ],
    tags = [
    ],
    deps = [
        ":scripts",
    ],
) for t in [
    "add",
    "arith",
    "common_sub",
    "compare",
    "compare2",
    "cse_basic",
    "dce1",
    "fflop",
    "flop",
    "gates",
    "grid_hier_test",
    "hierarchy",
    "issue_047",
    "logic_bitwise_op_gld",
    "loop_in_lg",
    "mt_basic_test",
    "mux",
    "mux2",
    "null_port",
    "offset_input",
    "operators",
    "params",
    "params_submodule",
    "pick",
    "punching",
    "reduce",
    "satsmall",
    "simple_flop",
    "simple_hier_test",
    "srasll",
    "submodule",
    "submodule2",
    "trivial",
    "trivial1",
    "trivial2",
    "trivial3",
    "trivial_and",
    "trivial_offset",
    "trivial_reduce",
    "unconnected",
]]

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
        "verilog.sh-long",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
) for t in VERILOG_LONG_TESTS]

exports_files(VERILOG_TESTS)
