# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

exports_files([
    "tests/pyrope/proc_lambda.prp",
    "tests/pyrope/simple.prp",
    "tests/comptime/trivial_if.prp",
])

# The pyrope/verilog golden pairs (used by the lhd flow-matrix tests).
filegroup(
    name = "equiv_tests",
    srcs = glob(["tests/equiv/*"]),
    visibility = ["//visibility:public"],
)

# Hierarchical Pyrope fixtures shared by the color / partition / abc pass tests
# (a combinational and a sequential multi-level design).
filegroup(
    name = "hier_tests",
    srcs = [
        "tests/pyrope/hier_comb.prp",
        "tests/pyrope/hier_seq.prp",
    ],
    visibility = ["//visibility:public"],
)

# pass.abc fixtures (task 2a-abc): a flat combinational Pyrope design plus a
# small self-contained Liberty so the ABC tech-map + LEC test is hermetic (no
# PDK dependency).
filegroup(
    name = "abc_tests",
    srcs = [
        "tests/abc/test.lib",
        "tests/pyrope/abc_arith.prp",
        "tests/pyrope/abc_comb.prp",
        "tests/pyrope/abc_mem.prp",
        "tests/pyrope/abc_seq.prp",
        "tests/pyrope/hier_seq.prp",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "inou_prp",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//lnast",
        "//pass/common:pass",
        "@prpparse//:prpparse",
    ],
    alwayslink = True,
)

# 2f-stream: the streaming seam keeps the CST arena bounded to one construct.
cc_test(
    name = "prpparse_stream_test",
    srcs = ["prpparse_stream_test.cpp"],
    copts = COPTS,
    deps = [
        "@googletest//:gtest_main",
        "@prpparse//:prpparse",
    ],
)

[
    sh_test(
        name = "prp-%s" % prp_file.split("/")[-1].split(".")[0],
        srcs = ["tests/pyrope_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:%s)" % prp_file,
        ],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/pyrope_test.py",
            "//inou/prp:%s" % prp_file,
            "//lhd",
        ],
    )
    for prp_file in glob([
        "tests/pyrope/*.prp",
        "tests/comptime/*.prp",
        "tests/verification/*.prp",
        "tests/errors/*.prp",
    ])
]

# Import tests (2f-lnimport): multi-file — an importer plus the unit(s) it
# imports. Each test names its inputs via the `:files:` header; every .prp in
# tests/imports/ is bundled into data so the header's repo-relative paths
# resolve at runtime. A `lib_*.prp` is an imported dependency only (no
# standalone target — it is meaningful only as another unit's import).
[
    sh_test(
        name = "prp-import-%s" % prp_file.split("/")[-1].split(".")[0],
        srcs = ["tests/pyrope_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:%s)" % prp_file,
        ],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/pyrope_test.py",
            "//lhd",
        ] + ["//inou/prp:%s" % f for f in glob(["tests/imports/*.prp"])],
    )
    for prp_file in glob(["tests/imports/*.prp"])
    if not prp_file.split("/")[-1].startswith("lib_")
]

# 2f-lgimport: the lg: import (compiled lgraph) two-step flow + LEC, plus the
# not-found / port-mismatch diagnostics, exercised from a self-contained script
# (it generates its own .v / .prp fixtures and drives `lhd compile` + `lhd
# check`). Needs the yosys scripts (lgcheck) on top of the lhd binary.
sh_test(
    name = "prp-import-lg",
    srcs = ["tests/imports/lg_import_test.sh"],
    data = ["//lhd"],
    deps = ["//inou/yosys:scripts"],
)

# 2f-defer — the cross-language `.[defer]` feedback (a Pyrope mod wires an
# imported Verilog module's output back into its input in the same cycle). Same
# two-step lg: flow + LEC as the lg import test.
sh_test(
    name = "prp-defer-mix",
    srcs = ["tests/imports/defer_mix_test.sh"],
    data = ["//lhd"],
    deps = ["//inou/yosys:scripts"],
)

# Equivalence pairs (tests/equiv/<name>.prp + golden <name>.v): the harness
# lowers the .prp to Verilog (tolg path) and formally LECs it against the
# golden via inou/yosys/lgcheck, so these need the yosys scripts/binary on
# top of the plain-prp data. Distinct `prp-equiv-` prefix: equiv/trivial_if
# would otherwise collide with the comptime/trivial_if target name.
[
    sh_test(
        name = "prp-equiv-%s" % prp_file.split("/")[-1].split(".")[0],
        srcs = ["tests/pyrope_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:%s)" % prp_file,
        ],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/pyrope_test.py",
            "//inou/prp:%s" % prp_file,
            "//inou/prp:%s" % (prp_file[:-len(".prp")] + ".v"),
            "//lhd",
        ],
        deps = ["//inou/yosys:scripts"],
    )
    for prp_file in glob(
        ["tests/equiv/*.prp"],
        # accum.prp is the aspirational call-site-phase file (stage[N]/@[N]):
        # no test header, no golden .v — it goes live with the call-site phase.
        exclude = [
            "tests/equiv/accum.prp",
        ],
    )
]

# 2f-v2prp: the REVERSE direction over the same equiv corpus — the slang reader
# lowers each golden <name>.v to Pyrope (upass/prp_writer), `lhd lec`
# re-compiles that Pyrope and LECs it against the same <name>.v. `lhd lec
# --set lec.solver=lgyosys` (yosys) is the authoritative gate; a timeout is inconclusive (not a
# fail). Driven off the .v files (every equiv pair ships one). All current pairs
# run < 20 s, so none are tagged `long`/`manual`.
[
    sh_test(
        name = "prp-v2prp-%s" % v_file.split("/")[-1].split(".")[0],
        srcs = ["tests/v2prp_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:%s)" % v_file,
        ],
        data = [
            "//inou/prp:tests/v2prp_test.py",
            "//inou/prp:%s" % v_file,
            "//lhd",
        ],
        deps = ["//inou/yosys:scripts"],
    )
    for v_file in glob(
        ["tests/equiv/*.v"],
        # assign_pattern.v is hierarchical: a parent wires two child modules
        # through `'{...}` unpacked-array output-port patterns, so the v2prp
        # round-trip (emit ONE Pyrope per module, recompile a submodule
        # standalone) does not apply — it is covered by its `:type: equiv_slang`
        # prp-equiv target instead. (arr2d_dyn_sel.v / bitsel_dyn.v used to be
        # excluded here too as known --reader slang dynamic-select miscompiles;
        # that lowering is now fixed in slang_expr.cpp lower_select, so they run.)
        exclude = [
            "tests/equiv/assign_pattern.v",
            # packed_assign.v uses a `'{...}` assignment pattern that yosys
            # read_verilog cannot parse (OP_CAST), so the v2prp reference side
            # (lgcheck reads the golden via yosys) cannot be built. Its
            # `--reader slang` reading is checked by prp-equiv-packed_assign
            # (:type: equiv_slang).
            "tests/equiv/packed_assign.v",
        ],
    )
]

# prp-yvr: the `--reader yosys-verilog` direction. The slang/pyrope tests above
# CANNOT catch the XiangShan MISCOMPILE class whose root cause is LiveHD's
# yosys-verilog front end (slang reads it correctly — verified sl=OK yv=BAD by
# xs/attribute2.sh), so this drives each minimal golden through
# `--reader yosys-verilog` and LECs the result vs the source:
#   * MISCOMPILE — clz_nest: the terminal constant of a deep nested-ternary chain
#                  is corrupted (CLZ_28/lzc/CMOUnit/... cluster, 68 LEC refutes).
# This reproduces a KNOWN, currently-unfixed yosys-verilog reader bug, so it FAILS
# by design. Tagged `fixme` (the repo's known-broken convention; .bazelrc filters
# `-fixme` so `bazel test //...` stays green). Run it to see the bug — clear the
# tag filter so the fixme target is selected — and a fix flips it green:
#   bazel test //inou/prp:prp-yvr-clz_nest --test_tag_filters=
[
    sh_test(
        name = "prp-yvr-%s" % base,
        srcs = ["tests/yvr_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:tests/equiv/%s.v)" % base,
        ],
        data = [
            "//inou/prp:tests/yvr_test.py",
            "//inou/prp:tests/equiv/%s.v" % base,
            "//lhd",
        ],
        tags = ["fixme"],
        deps = ["//inou/yosys:scripts"],
    )
    for base in [
        "clz_nest",  # MISCOMPILE: deep nested-ternary terminal constant
    ]
]

# prp->prp round-trip: a Pyrope source re-emitted through pass.prp_writer must
# recompile and LEC against its golden .v (the FORWARD companion of prp-v2prp).
# This locks in the constructs the writer fully supports — the pipeline
# (`stage[N]` / `@[N]`), `type_spec`, `tuple_concat`.  Curated allow-list: the
# prp->prp writer path is still maturing (many equiv pairs emit clean Pyrope
# that is not yet logically equivalent, and a handful hit unimplemented nodes
# the safety net now hard-fails — both tracked in todo/livehd/2w-prp_writer).
# Add a name here once its prp->prp round-trip LEC-passes.
[
    sh_test(
        name = "prp-p2p-%s" % base,
        srcs = ["tests/p2p_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:tests/equiv/%s.prp)" % base,
        ],
        data = [
            "//inou/prp:tests/p2p_test.py",
            "//inou/prp:tests/equiv/%s.prp" % base,
            "//inou/prp:tests/equiv/%s.v" % base,
            "//lhd",
        ],
        deps = ["//inou/yosys:scripts"],
    )
    for base in [
        "mod_call_pipe",       # pipe inlined into a mod: stage[N] + @[N]
        "pipe1_pass",          # bare pipe passthrough
        "pipe2_two_out",       # multi-output pipe
        "pipe3_mul",           # multi-stage arithmetic pipe
        "pipe_range",          # pipe[A..=B] range depth
        "pipe_state_acc",      # pipe with feedback state
        "mod_delay3",          # @[N] landing-cycle on a mod output
        "reg_out_feedforward",  # feedforward reg output
    ]
]
