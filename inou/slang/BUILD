# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "inou_slang",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@slang",  # Must be before pass because both have FMT and pass has priority (or slang fails)
    ] + [
        "//pass/common:pass",
    ],
    alwayslink = True,  # Needed to have constructor called
)

cc_binary(
    name = "rewriter",
    srcs = ["tests/rewriter.cpp"],
    copts = COPTS,
    deps = [
        ":inou_slang",
    ],
)

cc_binary(
    name = "slang",
    srcs = ["tests/slang.cpp"],
    copts = COPTS,
    deps = [
        ":inou_slang",
    ],
)

sh_test(
    name = "slang_test.sh",
    srcs = ["tests/slang_test.sh"],
    data = [
        "slang",
        "//inou/yosys:verilog_tests",
    ],
)

# FAIL: 18 tests passed:  add add1 add2 assigns compare2 consts issue_047 mt_basic_test offset_input satlarge satpick satsmall simple_weird2 trivial trivial3 trivial_and trivial_join trivial_offset
# inou/yosys/tests/multiassign.v

# FAIL: 69 tests failed:  arith arraycells async common_sub compare cprop cse_basic dce1 dce2 dce3 expression_00002 fflop flop gates graphtest grid_hier_test hier_test hierarchy issue_057 latch logic_bitwise_op_gld loop_in_lg loop_in_lg2 mem_reset mismatch mux mux2 nlatch blackboxing2 chunk_FetchTargetQueue cpp_api gcd_large join_fadd noloop not_vslogicnot null_port offset operators paramods params params_submodule pick punch.gld punch punching punching_3 reduce sample_stage1 shift shiftx shiftx_simple simple_add simple_flop simple_hier_test simple_rf1 simple_rf2 simple_weird srasll submodule submodule2 submodule_offset test trivial1 trivial2 trivial2a trivial_reduce tuplish unconnected wires


[sh_test(
    name = "slang_compile-%s" % t,
    srcs = ["tests/slang_compile.sh"],
    args = ["$(location //inou/yosys:tests/%s.v)" % t],
    data = [
        "//inou/yosys:tests/%s.v" % t,
        "//main:lgshell",
        "//inou/yosys:scripts",
    ],
    tags = [
        "slang_compile",
    ],
    deps = [
        "//inou/yosys:scripts",
        #"//inou/yosys:verilog_tests",
    ],
) for t in [
    "add",
    "add1",
    "add2",
    "assigns",
    "compare2",
    "consts",
    "issue_047",
    "mt_basic_test",
    "offset_input",
    "satlarge",
    "satsmall",
    "simple_weird2",
    "trivial",
    "trivial_and",
    "trivial_join",
    "trivial_offset",
]]
