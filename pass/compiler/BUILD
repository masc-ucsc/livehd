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
        "//inou/graphviz:inou_graphviz",
        "//inou/yosys:yosys2",
        "//pass/bitwidth:pass_bitwidth",
        "//pass/common:pass",
        "//pass/cprop:pass_cprop",
        "//pass/firmap:pass_firmap",
        "//pass/gioc:pass_gioc",
        "//pass/lnast_tolg:pass_lnast_tolg",
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
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "pyrope2lnast_compile",
    srcs = ["tests/pyrope2lnast_compile.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
    tags = ["fixme"],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# High Level FIRRTL
[sh_test(
    name = "firrtl_compile-%s" % t,
    srcs = ["tests/firrtl_compile.sh"],
    args = ["$(location //inou/firrtl:tests/proto/%s.hi.pb)" % t],
    data = [
        "//inou/firrtl:tests/proto/%s.hi.pb" % t,
        "//inou/firrtl:tests/verilog_gld/%s.gld.v" % t,
        "//inou/firrtl:post_io_renaming.py",
        "//main:lgshell",
    ],
    tags = [
        "firrtl_compile",
        "firrtl_compile-hi",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
) for t in [
    "Test2",
    "VecShiftRegisterSimple",
    "VecShiftRegisterParam",
    "VecShiftRegister",
    "Cell_alone",
    "Accumulator",
    "Coverage",
    "LFSR16",
    "TrivialAdd",
    "VendingMachineSwitch",
    "VendingMachine",
    "Trivial",
    "Tail",
    "TrivialArith",
    "NotAnd",
    "Shifts",
    "Darken",
    "HiLoMultiplier",
    "AddNot",
    "GCD_3bits",
    "Test3",
    "Register",
    "RegisterSimple",
    "Parity",
    "ResetShiftRegister",
    "SimpleALU",
    "ByteSelector",
    "MaxN",
    "Max2",
    "Flop",
    "EnableShiftRegister",
    "LogShifter",
    "Decrementer",
    "Counter",
    "RegXor",
    "BundleConnect",
    "SubModule",
    "SingleEvenFilter",
    "Xor6Thread2",
    "XorSelfThread1",
    "PlusAnd",
    "Mux4",
    "Adder4",
    "Life",
]]

# FIXME:
# Experimental Chirrtl extension
[sh_test(
    name = "firrtl_compile-%s" % t,
    srcs = ["tests/firrtl_compile.sh"],
    args = ["$(location //inou/firrtl:tests/proto/%s.ch.pb)" % t],
    data = [
        "//inou/firrtl:tests/proto/%s.ch.pb" % t,
        "//inou/firrtl:tests/verilog_gld/%s.gld.v" % t,
        "//inou/firrtl:post_io_renaming.py",
        "//main:lgshell",
    ],
    tags = [
        "firrtl_compile",
        "firrtl_compile-ch",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
) for t in [
    # issue1: runtime vector index
    # "Test1 VecSearch Mul"

    # issue3: memory
    # "Smem SmemStruct MaskedSmem MaskedSmemStruct Router ListBuffer"

    "IntXbar",
    "SimpleClockGroupSource",

    # "Arbiter_10",
    # "BankedStore",
    # "BoomDuplicatedDataArray",
    # "DynamicMemorySearch",
    # "MaskedSmem",
    # "MaskedSmemStruct",
    # "Memo",
    # "Router",
    # "Smem",
    # "SmemStruct",
]]

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
