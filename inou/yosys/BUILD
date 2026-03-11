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

YOSYS_COMPILE_TESTS = [
    ("add", "add"),
    ("add1", "add1"),
    ("add2", "add2"),
    ("aldff", "aldff"),
    ("arith", "arith"),
    ("arraycells", "arraycells"),
    ("assigns", "assigns"),
    ("common_sub", "common_sub"),
    ("compare", "compare"),
    ("compare2", "compare2"),
    ("consts", "consts"),
    ("cprop", "cprop"),
    ("cse_basic", "cse_basic"),
    ("dce1", "dce1"),
    ("dce2", "dce2"),
    ("dce3", "dce3"),
    ("expression_00002", "expression_00002"),
    ("fflop", "fflop"),
    ("flop", "flop"),
    ("gates", "gates"),
    ("graphtest", "graphtest"),
    ("grid_hier_test", "grid_hier_test"),
    ("hier_test", "hier_test"),
    ("hierarchy", "hierarchy"),
    ("issue_047", "issue_047"),
    ("issue_057", "issue_057"),
    ("latch", "latch"),
    ("logic_bitwise_op_gld", "logic_bitwise_op_gld"),
    ("loop_in_lg", "loop_in_lg"),
    ("loop_in_lg2", "loop_in_lg2"),
    ("mem_reset", "mem_reset"),
    ("mismatch", "mismatch"),
    ("mt_basic_test", "mt_basic_test"),
    ("multiassign", "multiassign"),
    ("mux", "mux"),
    ("mux2", "mux2"),
    ("nlatch", "nlatch"),
    # Keep the shorter target names while preserving nocheck behavior in the script.
    ("chunk_FetchTargetQueue", "nocheck_chunk_FetchTargetQueue"),
    ("gcd_large", "nocheck_gcd_large"),
    ("join_fadd", "nocheck_join_fadd"),
    ("noloop", "noloop"),
    ("not_vslogicnot", "not_vslogicnot"),
    ("null_port", "null_port"),
    ("offset", "offset"),
    ("offset_input", "offset_input"),
    ("operators", "operators"),
    ("paramods", "paramods"),
    ("params_submodule", "params_submodule"),
    ("params", "params"),
    ("pick", "pick"),
    ("punch.gld", "punch.gld"),
    ("punch", "punch"),
    ("punching_3", "punching_3"),
    ("punching", "punching"),
    ("random_delay", "random_delay"),
    ("reduce", "reduce"),
    ("sample_stage1", "sample_stage1"),
    ("satlarge", "satlarge"),
    ("satpick", "satpick"),
    ("satsmall", "satsmall"),
    ("shift", "shift"),
    ("shiftx_simple", "shiftx_simple"),
    ("shiftx", "shiftx"),
    ("simple_add", "simple_add"),
    ("simple_flop", "simple_flop"),
    ("simple_hier_test", "simple_hier_test"),
    ("simple_rf1", "simple_rf1"),
    ("simple_rf2", "simple_rf2"),
    ("simple_weird", "simple_weird"),
    ("simple_weird2", "simple_weird2"),
    ("Snxn1k", "Snxn1k"),
    ("srasll", "srasll"),
    ("submodule_offset", "submodule_offset"),
    ("submodule", "submodule"),
    ("submodule2", "submodule2"),
    ("test", "test"),
    ("trivial_and", "trivial_and"),
    ("trivial_join", "trivial_join"),
    ("trivial_offset", "trivial_offset"),
    ("trivial_reduce", "trivial_reduce"),
    ("trivial", "trivial"),
    ("trivial1", "trivial1"),
    ("trivial2", "trivial2"),
    ("trivial2a", "trivial2a"),
    ("trivial3", "trivial3"),
    ("tuplish", "tuplish"),
    ("unconnected", "unconnected"),
    ("wires", "wires"),
]

[sh_test(
    name = "yosys_compile-%s" % t[0],
    srcs = ["tests/yosys_compile.sh"],
    args = ["$(location //inou/yosys:tests/%s.v)" % t[1]],
    data = [
        "//inou/yosys:tests/%s.v" % t[1],
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
        "verilog.sh-long",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
) for t in VERILOG_LONG_TESTS]

exports_files(VERILOG_TESTS)
