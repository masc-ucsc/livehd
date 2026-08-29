# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")
load("//inou/slang:slang_ladder.bzl", "SLANG_LADDER")

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

# Verilog -> Pyrope emission must be byte-reproducible: the reader linearizes
# pointer-keyed sets, and a non-total sort key let generate-loop replicas
# permute, which RENAMES them through lname_of's `_sN` uniquing.
sh_test(
    name = "slang_emit_determinism",
    srcs = ["tests/slang_emit_determinism.sh"],
    data = ["//lhd"],
)

sh_test(
    name = "slang_test.sh",
    srcs = ["tests/slang_test.sh"],
    data = [
        "slang",
        "//inou/yosys:verilog_tests",
    ],
)

# `--reader slang -- -F filelist.f` passthrough: the verilog sources ride the
# raw slang driver args (a `-F` command file) with no positional `*.v` input.
sh_test(
    name = "slang_filelist",
    size = "large",
    srcs = ["tests/slang_filelist.sh"],
    data = glob(["tests/filelist/*"]) + ["//lhd"],
    deps = [
        "//inou/yosys:scripts",  # lgcheck + yosys2 for the LEC check
    ],
)

# Unknown-module (blackbox) policy: without a user --ignore-unknown-modules
# the compile fails cleanly; with it, unknown instances become blackbox
# sub-instances the pyrope emission imports, and supplying their .prp
# definitions LEC-proves against the golden.
sh_test(
    name = "slang_blackbox",
    size = "large",
    srcs = ["tests/slang_blackbox.sh"],
    data = glob(["tests/blackbox/*"]) + ["//lhd"],
)

# The --reader slang coverage ladder (todo/ 2s subtask E): one slang_compile-*
# target per inou/yosys/tests/*.v source, pinned to its strongest passing tier
# in slang_ladder.bzl (lec > verilog > lnast > error — see
# inou/slang/README.md). tests/slang_compile.sh enforces the expectation both
# ways, so a promotion or regression is an explicit ladder edit.
#
# Concurrency: inou.slang runs one isolated slang Driver/Compilation per file,
# sequentially. Parallelism comes from running independent bazel test actions
# concurrently — `bazel test //inou/slang:all` is the parallel sweep.

[sh_test(
    name = "slang_compile-%s" % t,
    size = "enormous" if t.startswith("long_") or t.startswith("nocheck_") else "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        SLANG_LADDER[t],
        "$(location //inou/yosys:tests/%s.v)" % t,
    ],
    data = [
        "//inou/yosys:tests/%s.v" % t,
        "//lhd",
    ],
    tags = [
        "slang_compile",
    ],
    deps = [
        "//inou/yosys:scripts",  # lgcheck + yosys2 for the lec tier
    ],
) for t in SLANG_LADDER.keys()]

# Legacy no-argument mode: the sky130 cell-instance set (unknown-module policy).
sh_test(
    name = "slang_compile_sky130",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    data = glob(["tests/verilog/*"]) + ["//lhd"],
    tags = [
        "slang_compile",
    ],
)

# Named structured assignment patterns are resolved by field, with explicit
# member setters overriding `default:`. This syntax is a slang-reader contract;
# the legacy Yosys parser used by the other compile suite does not accept it.
#
# tests/sv/ is the home for slang-only POSITIVE fixtures, because neither sweep
# directory can hold one: tests/verilog/ is swept at the `error` tier (a file
# that compiles is a FAILURE there), and //inou/yosys:tests/*.v is globbed into
# yosys_compile-* targets that this syntax would break. Pin new positive
# slang-reader fixtures here, one explicit target each.
sh_test(
    name = "slang_struct_pattern_named_default",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/struct_pattern_named_default.v)",
    ],
    data = [
        "tests/sv/struct_pattern_named_default.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# CVA6 forms whose constants become visible only after function/config binding:
# preserve `2 ** cfg.field` as a shift and a canonical cfg-bounded loop as an
# LNAST range loop. Also pins zero-fill assignment patterns and whole-array
# copies on unpacked arrays of packed structs, including a two-dimensional
# split-memory next-state copy and a full unpacked-array slice.
sh_test(
    name = "slang_deferred_cva6_forms",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/deferred_cva6_forms.v)",
    ],
    data = [
        "tests/sv/deferred_cva6_forms.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# A bound that remains runtime after uPass must fail at the comptime-loop
# boundary, not in inou.slang and not by silently dropping the loop body.
sh_test(
    name = "slang_deferred_for_runtime",
    size = "large",
    srcs = ["tests/deferred_for_runtime_smoke.sh"],
    args = ["$(location tests/sv/deferred_for_runtime.v)"],
    data = [
        "tests/sv/deferred_for_runtime.v",
        "//lhd",
    ],
)

# Storage-aware scalar replacement of a packed scalar-element array. Constant
# element accesses route directly to leaves, a dynamic read preserves the
# SystemVerilog selector semantics, and whole-value use reassembles only at the
# output boundary.
sh_test(
    name = "slang_packed_array_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# Generated partial writes to consecutive packed-array elements form an RMW
# chain. The final output must observe the last generated stage, even when its
# source-level assignment appears before the generate loop.
sh_test(
    name = "slang_generated_packed_stage_chain",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/generated_packed_stage_chain.v)",
    ],
    data = [
        "tests/sv/generated_packed_stage_chain.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# A nested reset/enable write to one packed bit must not make ABC select the
# entire carrier once per condition. cprop factors the Mux through Set_mask so
# only the one-bit lane is Boolean logic; lgyosys independently checks the
# emitted result against the source.
sh_test(
    name = "slang_conditional_packed_lane_update",
    size = "large",
    srcs = ["tests/conditional_packed_lane_update_smoke.sh"],
    data = [
        "tests/sv/conditional_packed_lane_update.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# Two DISJOINT sub-word writes of DIFFERENT widths into one word of a packed 2-D
# reg (minion's `ex_reg_data`). A memory carries one `wensize`, so it can hold
# neither a mixed-width per-chunk enable nor two read-modify-write splices into a
# clocked word -- the reader keeps such an array a flat flop bus instead, and
# lgyosys checks the result against the source.
sh_test(
    name = "slang_packed2d_mixed_partial_write",
    size = "large",
    srcs = ["tests/packed2d_mixed_partial_write_smoke.sh"],
    data = [
        "tests/sv/packed2d_mixed_partial_write.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# A scalar register with a Verilog declaration / initial-block value but no
# explicit reset is modeled as reset-bearing state using that value. This is a
# deliberate two-state synthesis choice and carries a warning because source
# LEC can distinguish it from truly reset-less hardware.
sh_test(
    name = "slang_scalar_initial_no_reset",
    size = "large",
    srcs = ["tests/scalar_initial_no_reset_test.sh"],
    data = [
        "tests/sv/scalar_initial_no_reset.v",
        "//lhd",
    ],
)

sh_test(
    name = "slang_packed_vector_port_sroa",
    size = "large",
    srcs = ["tests/packed_vector_port_sroa_test.sh"],
    data = [
        "tests/sv/packed_vector_port_sroa.v",
        "//lhd",
    ],
)

# `{{N{v[msb]}}, v}` is a sign extension, not a broadcast + shift + or. Pins
# both halves: the shapes that MUST fold to `sext`, and the look-alikes that
# must not.
sh_test(
    name = "slang_concat_sext",
    size = "large",
    srcs = ["tests/concat_sext_test.sh"],
    data = [
        "tests/sv/concat_sext.v",
        "//lhd",
    ],
)

# An instance inside a generate block must carry the generate prefix in its name
# exactly as a net inside one does. Two genvar-loop instances of the same module
# otherwise share a name, and after flattening so do their registers — which
# pass/abc's register read-back then disambiguates on the IMPL side only,
# refuting the post-synthesis LEC.
sh_test(
    name = "slang_genblk_instance_name",
    size = "large",
    srcs = ["tests/genblk_instance_name_test.sh"],
    data = [
        "tests/sv/genblk_instance_name.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",  # lgcheck + yosys2 for the LEC leg
    ],
)

# An instance named after its own module, of a module with NO OUTPUTS
# (bedrock-rtl's spelling for every assertion-only observer). Both refs of the
# emitted `fcall` carry the same name, which made prp_writer read the CALLEE
# reference as a read of the call's own result and refuse the whole module.
sh_test(
    name = "slang_self_named_sink_inst",
    size = "large",
    srcs = ["tests/self_named_sink_inst_test.sh"],
    data = [
        "tests/sv/self_named_sink_inst.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",  # lgcheck + yosys2 for the LEC leg
    ],
)

# A plain packed local whose first lowered write sits inside a comptime-decided
# `if` arm used to lose its declaration entirely (the arm, and the declare with
# it, is dead-code-eliminated), so the emitted Pyrope did not recompile. Round
# trip, because the first leg emits fine.
sh_test(
    name = "slang_comptime_arm_declare_hoist",
    srcs = ["tests/comptime_arm_declare_hoist_test.sh"],
    data = [
        "tests/sv/comptime_arm_declare_hoist.v",
        "//lhd",
    ],
)

# The writer renders a chain of N single-use temps as ONE expression nested N
# deep (render_def_rhs recurses per level) on a worker thread. A 1000-deep
# shl/or chain -- the shape CVA6's perf_counters packed-array writes lower to --
# used to die at the worker's 512 KiB stack guard page (`Bus error: 10`) on
# every non-opt build. Round trip: the emit AND the recompile of the emitted,
# equally deep, expression. The input is generated by the script.
sh_test(
    name = "slang_deep_fold_chain",
    srcs = ["tests/deep_fold_chain_test.sh"],
    data = [
        "//lhd",
    ],
)

# A local net named like a module the unit instantiates (`logic popcount` +
# `popcount #(...) i_popcount`, CVA6's instr_queue). Verilog keeps the two in
# separate namespaces; the emitted Pyrope does not, so the file-scope import
# alias must step aside or the instantiation re-reads as a call to the local's
# value (`call to undefined function '0'`). Round trip.
sh_test(
    name = "slang_local_shadows_callee",
    srcs = ["tests/local_shadows_callee_test.sh"],
    data = [
        "tests/sv/local_shadows_callee.v",
        "//lhd",
    ],
)

# Packed 2-D array eligibility, both directions: a net-initialized lane table
# read at a runtime index must SPLIT (barrel shift -> lane mux), and an array
# written through a whole-array range select must stay FLAT (the split
# representations drop the write).
sh_test(
    name = "slang_packed_array_net_init_mux",
    size = "large",
    srcs = ["tests/packed_array_net_init_mux_test.sh"],
    data = [
        "tests/sv/packed_array_net_init_mux.v",
        "//lhd",
    ],
)

sh_test(
    name = "slang_packed_array_reg_array_emit",
    size = "large",
    srcs = ["tests/packed_array_reg_array_emit_test.sh"],
    data = [
        "tests/sv/packed_array_reg_array_emit.v",
        "//lhd",
    ],
)

sh_test(
    name = "slang_packed_array_wire_init_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_wire_init_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_wire_init_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "slang_packed_array_reg_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_reg_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_reg_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# An instance output read ONLY as the index of an array element whose field is
# accessed (`data_i[bin].tag`) must still count as a READ of that net, or the
# net keeps its poison initializer and the instance binding is dropped as dead.
sh_test(
    name = "slang_instance_output_as_member_index",
    size = "large",
    srcs = ["tests/instance_output_member_index_test.sh"],
    args = ["$(location tests/sv/instance_output_as_member_index.v)"],
    data = [
        "tests/sv/instance_output_as_member_index.v",
        "//lhd",
    ],
)

# Disjoint generated always_ff blocks over one flattened packed register must
# combine their slice resets into one asynchronous reset. Losing that metadata
# turns reset assertion without a clock edge into a stale-state mismatch.
sh_test(
    name = "slang_async_reset_slices",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/async_reset_slices.v)",
    ],
    data = [
        "tests/sv/async_reset_slices.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# State omitted from an async reset arm still holds while reset is asserted.
# Peeling the reset into attributes must retain that process-level write gate.
sh_test(
    name = "slang_async_reset_unreset_state",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/async_reset_unreset_state.v)",
    ],
    data = [
        "tests/sv/async_reset_unreset_state.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# A submodule may feed a pure flop/latch output back into one of its own
# inputs. The input must bind the resolved output net, not the output port's
# temporary X initializer created before the instance output is assigned.
sh_test(
    name = "slang_registered_instance_output_feedback",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/registered_instance_output_feedback.v)",
    ],
    data = [
        "tests/sv/registered_instance_output_feedback.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# Unpacked module ports flatten in declaration order: the rightmost array
# element occupies the packed boundary bus LSB. Pin both range directions and
# both reads and writes, including a dynamic element select.
sh_test(
    name = "slang_unpacked_array_port_order",
    size = "large",
    srcs = ["tests/unpacked_array_port_order_test.sh"],
    args = [
        "$(location //lhd)",
        "$(location tests/sv/unpacked_array_port_order.v)",
    ],
    data = [
        "tests/sv/unpacked_array_port_order.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "slang_packed_array_loop_reg_sroa",
    size = "large",
    srcs = ["tests/packed_array_loop_reg_sroa_test.sh"],
    args = ["$(location tests/sv/packed_array_loop_reg_sroa.v)"],
    data = [
        "tests/sv/packed_array_loop_reg_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# Ascending packed indices deliberately reverse source index versus physical
# lane ordinal. Dynamic access includes out-of-range values and whole-value
# packing must keep the declared [0:3] ordering.
sh_test(
    name = "slang_packed_array_ascending_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_ascending_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_ascending_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "slang_packed_array_signed_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_signed_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_signed_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "slang_packed_array_port_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_port_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_port_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "slang_packed_array_struct_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_struct_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_struct_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "slang_packed_array_struct_reg_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_struct_reg_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_struct_reg_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# Sub-word writes into a MULTI-DIMENSIONAL unpacked array — the bedrock-rtl
# `br_ram_flops` tiling idiom `tile_wr_data[r][c][wport] = …`, which used to die
# on "nested non-variable assignment targets are not supported yet" because both
# memory-element write paths hard-coded a SINGLE selector. The combinational
# fixture also pins the packed-bus poison seed a linearized comb array needs
# before its first element store, and the memory fixture pins that same-cycle
# disjoint chunk writes still take the write-enable model (a read-modify-write
# would clobber one of them).
sh_test(
    name = "slang_array2d_partial_write",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/array2d_partial_write.v)",
    ],
    data = [
        "tests/sv/array2d_partial_write.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "slang_array2d_mem_partial_write",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/array2d_mem_partial_write.v)",
    ],
    data = [
        "tests/sv/array2d_mem_partial_write.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# A slice write into one entry of a STRUCT-ELEMENT (tuple) memory: decomposed
# across the per-field memories detuple splits out, instead of a store on the
# aggregate name that no longer exists (a silently DROPPED write). Emission
# check, not LEC — see the header of the test script for why a LEC verdict says
# nothing about a split tuple memory.
sh_test(
    name = "slang_mem_struct_slice_write",
    size = "large",
    srcs = ["tests/mem_struct_slice_write_test.sh"],
    data = [
        "tests/sv/mem_struct_slice_write.v",
        "//lhd",
    ],
)

# Two same-cycle sub-word writes to ONE clocked-memory word at a granularity the
# chunk-enable model cannot express: they cannot be merged (both read-modify-write
# the committed word), so the reader must DIAGNOSE the second site rather than
# emit hardware that silently loses a write. Pinned at the `error` tier — a
# promotion here means the merge became expressible and this ladder entry should
# move with it.
sh_test(
    name = "slang_mem_double_partial_write",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "error",
        "$(location tests/sv/mem_double_partial_write.v)",
    ],
    data = [
        "tests/sv/mem_double_partial_write.v",
        "//lhd",
    ],
)
