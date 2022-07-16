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
    # "vec_shift_register_param", FIXME: no input connect to the shift register?
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

# CHIRRTL extension
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
    "UOPCodeFDivDecoder",
    "CompareRecFN",
    "RegisterReadDecode",
    "RenameBusyTable",
    "RoundAnyRawFNToRecFN",
    "INToRecFN",
    "ForwardingAgeLogic",
    "PMPChecker_1",
    "HellaCacheArbiter",
    "NullPrefetcher",                                                                                                                                                                      
    "NullLocalBranchPredictorBank",                                                                                                                                                        
    "RegisterFile",
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


# note: patterns with tag of "firrtl_compile_compiles" are either compiles or long LEC time
[sh_test(
    name = "firrtl_compile_compiles-%s" % t,
    srcs = ["tests/firrtl_compile_compiles.sh"],
    args = ["$(location //inou/firrtl:tests/proto/%s.ch.pb)" % t],
    data = [
        "//inou/firrtl:tests/proto/%s.ch.pb" % t,
        "//inou/firrtl:post_io_renaming.py",
        "//main:lgshell",
    ],
    tags = [
        "firrtl_compile",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
) for t in [
    "FetchBuffer",                   # LEC-? (long)
    "BranchDecode",                  # LEC-x (1st priority)
    "BoomRAS",                       # LEC-x (1st priority, no bits set on some mux!?)
    "RVCExpander_2",                 # LEC-o
    "BoomProbeUnit",                 # LEC-? (long)
    "BoomIOMSHR",                    # LEC-? (long)
    "FMADecoder_1",                  # LEC-o
    "Mul54",                         # LEC-?
    "PipelinedMultiplier",           # LEC-o
    "RecFNToIN",                     # LEC-o
    "PredRenameStage",               # LEC-o
    "RenameMapTable",                # LEC-x
    "RenameFreeList",                # LEC-x
    "IssueUnitCollapsing",           # LEC-?
    "NBDTLB",                        # LEC-x (all leave pass!?)
    "BoomDuplicatedDataArray",
    "FPUFMAPipe_1",
    "MulAddRecFNPipe_1",
    "MulAddRecFNToRaw_postMul_1",
    "DivSqrtRecF64",
    "DivSqrtRecF64_mulAddZ31",
    "FPU",
    "FPUUnit",
    "FDivSqrtUnit",
    "MulDiv",
    "IssueSlot_1",
    "CSRFile",
    "RegisterFileSynthesizable",
    "ALUUnit",
    "FPToInt",
    "ALU",
    "DecodeUnit",
    "DivUnit",
    "MulAddRecFNToRaw_preMul",
    "DivSqrtRecF64ToRaw_mulAddZ31",
    "MemAddrCalcUnit",
    "PipelinedMulUnit",
    "RecFNToRecFN",
    "RegisterRead",
    "ALUExeUnit",                    # ?LEC
    "IntToFP",                       # !LEC
    "IntToFPUnit",                   #  LEC
    "FPToFP",                        #  LEC
    "Smem",
    "SmemStruct",
    "MaskedSmem",
    "MaskedSmemStruct",
]]

# FIXME:
# issue#1: memory
# issue#2: bitwidth mismatch on memory output *****
#          assertion it->second.get_sbits() <= addr_sbits *****
# "Rob" 
# "FPUExeUnit"

# issue#4: non-of the TupAdds are solved after cprop because if
#          tuple_issues flag remains. And the reason is that the
#          n150_mux tries to use TGs to get result from Mem 
# "MemBranchSimplified"
# "BranchKillableQueue_3"
# "BranchKillableQueue_4"
# "BranchKillableQueue_5"
# "FpPipeline"
# "PTW"

# Intro
# Related
# LNAST + eval  
# LGrph + eval
# Para + eval
# Conclusion


# BoomTile
# - D1: BoomNonBlockingDCache ----------------------------------------- (x) // wait for mem                                                                                                                                                                       
# - D1: IntSyncSyncCrossingSink --------------------------------------- (o)                                                                 
# - D1: IntSyncAsyncCrossingSink -------------------------------------- (o)                                                                 
#     - D2: SynchronizerShiftReg_w1_d3                                                           
#         - D3: NonSyncResetSynchronizerPrimitiveShiftReg_d3
# - D1: BundleBroadcast_2 --------------------------------------------- (o)                                          
# - D1: LSU                                
#     - D2: ForwardingAgeLogic ---------------------------------------- (o)                                                                   
#     - D2: NBDTLB ---------------------------------------------------- (o)                                                                               
#         - D3: package_Anon_43 --------------------------------------- (o)                                                                  
#         - D3: PMPChecker_1 ------------------------------------------ (o)
# - D1: HellaCacheArbiter --------------------------------------------- (o)

# - D1: TLBuffer_11                                                                                                                                                                                 
#     - D2: Queue_37 -------------------------------------------------- (x) // wait for mem                                                                                                                                                                                
#     - D2: TLMonitor_59                                                                                                                                                                            
#         - D3: plusarg_reader_59 (*EXTERNAL) ------------------------- (x) // wait for external                                                                                                                                                          
# - D1: TLXbar_8                         
#     - D2: TLMonitor_57                 
#         - D3: plusarg_reader_57 (*EXTERNAL) ------------------------- (x) // wait for external
# - D1: BoomNonBlockingDCache                                                                                                                                                                       
#     - D2: BoomMSHRFile                                                                                                                                                                            
#         - D3: NullPrefetcher ---------------------------------------- (o)                                                                                                                                                                      
#         - D3: Arbiter ----------------------------------------------- (o)                                                                                                                                                                            
#         - D3: BoomIOMSHR -------------------------------------------- (o)                                                                                                                                                                          
#         - D3: BranchKillableQueue_2 --------------------------------- (x) // wait for mem                                                                                                                                                               
#         - D3: BoomMSHR ---------------------------------------------- (x) // wait for mem                                                                                                                                                                           
#             - D4: BranchKillableQueue ------------------------------- (x) // wait for mem                                                                                                                                                           
#     - D2: AMOALU ---------------------------------------------------- (o)                                                                                                                                                                                  
#     - D2: MaxPeriodFibonacciLFSR_1 ---------------------------------- (o)
#     - D2: BoomWritebackUnit ----------------------------------------- (i) // TODO
#     - D2: BoomDuplicatedDataArray ----------------------------------- (i) // TODO
#     - D2: BoomProbeUnit --------------------------------------------- (o)   
#     - D2: L1MetadataArray ------------------------------------------- (x) // wait for mem
# - D1: BoomFrontend                                                                                                                                                                                
#     - D2: RVCExpander_2 --------------------------------------------- (o)                                                                                                                                                                           
#     - D2: Queue_30 -------------------------------------------------- (x) // wait for mem                                                                                                                                                                                   
#     - D2: BranchPredictor                                                                                                                                                                         
#         - D3: ComposedBranchPredictorBank                                                                                                                                                         
#             - D4: TageBranchPredictorBank --------------------------- (x) // wait for mem: some inputs of __fir_cat is not ready                                                                                                                                                         
#                 - D5: TageTable ------------------------------------- (x) // wait for mem and ""!!!unknown IO bits"
#             - D4: BTBBranchPredictorBank ---------------------------- (i) // TODO: fir2ln assignment issue, solvable
#             - D4: BIMBranchPredictorBank ---------------------------- (i) // TODO: fir2ln assignment issue, solvable
#             - D4: FAMicroBTBBranchPredictorBank --------------------- (i) // TODO: ***** hard one ***** TA sink-pin multiple-drivers??                                                                                                                                                 
#             - D4: LoopBranchPredictorBank --------------------------- (i) // TODO: ***** hard one ***** TA sink-pin multiple-drivers??                                                                                                                                                          
#                 - D5: LoopBranchPredictorColumn --------------------- (o) // no-bits register!!!?                                                                                                                                                 
#         - D3: NullLocalBranchPredictorBank -------------------------- (o)                                                                                                                                                        
#     - D2: TLB                                                                                                                                                                                     
#         - D3: package_Anon_1 ---------------------------------------- (o) 
#         - D3: PMPChecker -------------------------------------------- (o)
#     - D2: BreakpointUnit_3 ------------------------------------------ (o)
#     - D2: FetchBuffer
#     - D2: ICache -----------------------------------------------------(x) // wait for mem: some inputs of __fir_eq is not ready
#         - D3: MaxPeriodFibonacciLFSR_2 ------------------------------ (o)
#     - D2: BoomRAS --------------------------------------------------- (o) 
#     - D2: FetchTargetQueue ------------------------------------------ (x) // wait for mem
#     - D2: BranchDecode ---------------------------------------------- (o) 


# - D0: DinoPipelinedBP
#     - D1: PipelinedCPUBP
#         - D2: HazardUnitBP ------------------------------------------ (o) # LEC-o
#         - D2: Adder_1 ----------------------------------------------- (o) # LEC-o
#         - D2: AlwaysNotTakenPredictor ------------------------------- (o) # LEC-o
#         - D2: Control ----------------------------------------------- (o) # LEC-o
#         - D2: ALUControl -------------------------------------------- (o) # LEC-o
#         - D2: Adder ------------------------------------------------- (o) # LEC-o
#         - D2: ImmediateGenerator ------------------------------------ (o) # LEC-o
#         - D2: ForwardingUnit ---------------------------------------- (o) # LEC-o
#         - D2: NextPC ------------------------------------------------ (i) # LEC-x
#         - D2: NextPCSimple ------------------------------------------ (i) # LEC-x: the fir_is_sint problem, discuss with Jose
#                                                                                    (1) one firrtl lessThen op needs two operands, asSint(io.x) and asSint(io.y)
#                                                                                    (2) after BW, the n111_attr_set is set with ubits, but the n79_lt needs signed comparasion.
#                                                                                    (3) also mention bitwidth.cpp: 1201 to 1205 on how to avoid unnecessary tposs insertion
#         - D2: ALU --------------------------------------------------- (i) # LEC-x: the same fir_is_sint issue as NextPC
#         - D2: RegisterFile ------------------------------------------ (o) # LEC-o: see below, discuss with Jose
#         - D2: RegisterFileSimple------------------------------------- (o) # LEC-o: (1) n106_mux got visited twice at the same lg->forward()
#                                                                                    (2) n119_mux disconnected from n106_mux and reconnected to a 0?
#                                                                                        search FIXME->sh in lgtuple.cpp
#                                                                                    (3) hacky patch, the problem should be solved at the lgtuple level
#         - D2: StageReg ---------------------------------------------- (o) # LEC-o
#         - D2: StageReg_1 -------------------------------------------- (o) # LEC-o
#         - D2: StageReg_2 -------------------------------------------- (o) # LEC-o
#         - D2: StageReg_3 -------------------------------------------- (o) # LEC-o
#         - D2: StageReg_4 -------------------------------------------- (o) # LEC-o
#         - D2: StageReg_5 -------------------------------------------- (o) # LEC-o
#         - D2: StageReg_6 -------------------------------------------- (o) # LEC-o 
#     - D1: DualPortedCombinMemory ------------------------------------ (x) # wait for mem
#     - D1: DCombinMemPort -------------------------------------------- (i) # firbit fb not found!?
#     - D1: ICombinMemPort -------------------------------------------- (i) # firbit fb not found!?



