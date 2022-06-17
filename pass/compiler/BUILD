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
        # "//inou/yosys:yosys2",
        "//pass/bitwidth:pass_bitwidth",
        "//pass/common:pass",
        "//pass/cprop:pass_cprop",
        "//pass/firmap:pass_firmap",
        "//pass/lnast_tolg:pass_lnast_tolg",
    ],
    alwayslink = True,
)

[sh_test(
    name = "pyrope_compile-%s" % t,
    srcs = ["tests/pyrope_compile.sh"],
    args = ["$(location //inou/pyrope:tests/%s.prp)" % t],
    data = [
        "//inou/pyrope:tests/%s.prp" % t,
        "//inou/pyrope:tests/verilog_gld/%s.gld.v" % t,
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
    tags = [
        "pyrope_compile",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
) for t in [
    "array",
    "scalar_tuple",
    "flatten_bundle",
    "partial",
    "hier_tuple",
    "reg_bits_set",
    "bits_rhs",
    "reg__q_pin",
    "hier_tuple_io",
    "hier_tuple3",
    "tuple_if",
    "ssa_rhs",
    "out_ssa",
    "attr_set",
    "lhs_wire",
    "tuple_copy",
    "if1",
    "lhs_wire2",
    "tuple_copy2",
    "counter_1",
    "adder_stage",
    "logic2",
    "tuple_empty_attr",
    "if2",
    "scalar_reg_out_pre_declare",
    "firrtl_tail2",
    "hier_tuple_nested_if3",
    "hier_tuple_nested_if5",
    "hier_tuple_nested_if6",
    "hier_tuple_nested_if7",
    "firrtl_tail",
    "nested_if",
    "counter_nested_if",
    # "tuple_reg", incorrect lgraph creation with a weird dp_assign 0 in value (no way to constrain)
    "tuple_reg2",
    "tuple_nested1",
    "tuple_nested2",
    "get_mask1",
    "vec_shift_register_param",
    "hier_tuple2",
    "capricious_bits",
    "capricious_bits2",
    "capricious_bits4",
    "hier_tuple_nested_if",
    "hier_tuple_nested_if2",
    "struct_flop",
    "hier_tuple_nested_if4",
    "firrtl_gcd_3bits",
    "firrtl_tail3",
    "connect_through2",
    "direct_io_assign",
    "bw_overflow",
    "mem_comb1",
    #"connect_through",  FIXME: re-enable
    "top",
]]

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
    "Test1",
    "VecSearch",
    "VecShiftRegister",
    "BundleConnect",
    "SingleEvenFilter",
    "Xor6Thread2",
    "XorSelfThread1",
    "Mux4",
    "Life",
    "Cell_alone",
    "LFSR16",
    "LogShifter",
    "Test2",
    "Accumulator",
    "Coverage",
    "TrivialAdd",
    "VendingMachineSwitch",
    "VendingMachine",
    "Trivial",
    "Tail",
    "TrivialArith",
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
    "Decrementer",
    "Counter",
    "RegXor",
    "PlusAnd",
]]

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
    "Mul",
    "IntXbar",
    "SimpleClockGroupSource",
    "FixedClockBroadcast",
    "ClockGroupAggregator",
    "IntSyncSyncCrossingSink",
    "Adder4",
    "AMOALU",
    "Top",
    "Arbiter_10",
    "FlipSimple2",
    "NotAnd",
    "BreakpointUnit",
    "DebugCustomXbar",
    "RenameMapTableSimpleF5",
    "MaxPeriodFibonacciLFSR",
    "BranchMaskGenerationLogic",
    "BasicDispatcher",
    "FMADecoder",
    "UOPCodeFPUDecoder",
    "UOPCodeFDivDecoder"

    # FIXME:
    # issue-1: memory
    # "Smem",
    # "SmemStruct",
    # "MaskedSmem",
    # "MaskedSmemStruct",

    # issue-1-1: lconst.cpp:329
    # "Rob"

    # issue-3: assert bw_sbits fail
    # "DecodeUnit"
    # "MulDiv"
    # "RenameBusyTable_*"
    # "PredRenameStage"

    # issue-4: firbits cannot converge within 10 iterations: 
    # possible solution: set_bit() flop qpin directly
    # "RegisterFileSynthesizable"
    # "RegisterFileSynthesizable_1"
    # "RegisterFileSynthesizable_2"
    # "BranchKillableQueue_3"
    # "BranchKillableQueue_4"
    # "BranchKillableQueue_5"
    
    # issue-5: non-LEC
    # "ALU"
    # "RenameMapTable"

    # issue-6: no bits set at verilog-gen
    # "CompareRecFN"
    # "RecFNToIN"
    # "RecFNToIN_1"

    # issue-7: cannot find register qpin
    # "CSRFile"

    # issue-A: need last value of a variable??
    # "DivSqrtRecF64ToRaw_mulAddZ31"

    # issue-B: non-flattend wire variable _T_ctrl.br_type??
    # "IssueSlot*"

    # issue-C: firbits signness unequal
    # "MulAddRecFNToRaw*"
    # "RoundAnyRawFNToRecFN*"

    # issue-D: boost cannot shift by a negative value
    # right shift cannot guarantee, might be able to be commented
    # "RegisterReadDecode"

    # issue-n: ??? 

    # BoomCore passing submodules
    # "Arbiter_15"
    # "Arbiter_16"
    # "Arbiter_17"
    # "Arbiter_18"
    # "FMADecoder_1"
    # "FMADecoder_2"
    # "Mul54"
    # "PipelinedMultiplier"
    
    
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
