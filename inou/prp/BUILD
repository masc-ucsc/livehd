# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:cc_headers.bzl", "cc_direct_headers")
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
        "tests/pyrope/abc_mathops.prp",
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

# Expected-warning tests (`:type: warning`, in tests/warnings/): the program
# must COMPILE CLEANLY but emit a warning diagnostic whose message/hint match
# the header :warning:/:help: regexes (the lint counterpart of tests/errors/).
# A `locate_warning_here` comment pins the warning's line. Distinct `prp-warn-`
# prefix so the target names cannot collide with the `prp-` glob above.
[
    sh_test(
        name = "prp-warn-%s" % prp_file.split("/")[-1].split(".")[0],
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
    for prp_file in glob(["tests/warnings/*.prp"])
]

# TBD-feature trackers (tests/fixme/): each `.prp` exercises a documented but
# NOT-yet-implemented Pyrope feature (marked "TBD" in the docs / 15-tbd.md). Most
# are `:type: compile` (the feature has no lowering, so the compile FAILS by
# design); `testbench_step` is `:type: simulation` (the instance/`step`/field-poke
# model runs, but the string-path `peek("a/b")`/`poke("a/b",v)` accessors are
# still unimplemented). Each flips green once the feature lands — at which point
# drop the `fixme` tag and move the file into the
# normal glob above. Tagged `fixme` so `.bazelrc` (`-fixme`) keeps `bazel test
# //...` green; run one with the tag filter cleared to see the failure:
#   bazel test //inou/prp:prp-tbd-temporal_past --test_tag_filters=
# Covered: temporal library (past[n]), standard library (import("prp")),
# verification extras (requires/ensures), and testbench extras (step/peek/poke).
# (The `lg` attribute used to be on this list but is now implemented — its TBD
# markers were removed from the docs.) The `macro=` memory-compiler binding is
# also still TBD but is silently accepted-and-ignored — it compiles clean today
# (no diagnostic), so it needs an emitted-Verilog grep harness, not a compile
# test, and is tracked in todo/pyrope/3f-macro.html instead.
[
    sh_test(
        name = "prp-tbd-%s" % base,
        srcs = ["tests/pyrope_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:tests/fixme/%s.prp)" % base,
        ],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/pyrope_test.py",
            "//inou/prp:tests/fixme/%s.prp" % base,
            "//lhd",
        ],
        tags = ["fixme"],
    )
    for base in [
        "temporal_past",
        "stdlib_import",
        "verif_requires",
        "testbench_step",
    ]
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

# Simulation tests (tests/sim/*.prp, `:type: simulation`): a synthesizable DUT
# driven from `test` blocks that end-of-sim `assert` the result. The harness
# (`lhd sim --setup-only`) lowers each DUT to header-only Slop<N> C++ and emits
# a driver per test; prplib then compiles each driver with the HOST C++ compiler
# and runs it (a non-zero exit == an assert fired). The Slop/Blop runtime is
# header-only and, with -DNDEBUG, has no link deps — so this needs NO nested
# bazel, just hlop's + iassert's headers staged into runfiles (the
# `sim_runtime_hdrs` data dep below), discovered by name at runtime.
cc_direct_headers(
    name = "sim_runtime_hdrs",
    deps = [
        "@hlop//hlop",
        "@iassert",
    ],
    visibility = ["//visibility:private"],
)

[
    sh_test(
        name = "prp-sim-%s" % prp_file.split("/")[-1].split(".")[0],
        srcs = ["tests/pyrope_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:%s)" % prp_file,
        ],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/prpsim.py",
            "//inou/prp:tests/pyrope_test.py",
            "//inou/prp:%s" % prp_file,
            "//inou/prp:sim_runtime_hdrs",
            "//lhd",
        ],
    )
    for prp_file in glob(["tests/sim/*.prp"])
]

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
            # signed_one_bit_cmp: known-failing miscompile tracker (1-bit signed
            # ordered compare reads unsigned). Run via the fixme target below.
            "tests/equiv/signed_one_bit_cmp.prp",
            # gen_type_cast: DELIBERATELY-broken generics repro — the `T(a)`
            # constructor on a generic param does not lower yet (see the file
            # header, tracked by 3g). It cannot compile, so the forward equiv
            # test can never pass; run it via the fixme target below. The reverse
            # direction is already fixme-tagged through _V2PRP_FIXME.
            "tests/equiv/gen_type_cast.prp",
        ],
    )
]

# Known-failing equiv tracker (fixme): a MISCOMPILE that compiles clean but LECs
# wrong, so it cannot live in tests/fixme/ (compile-only). Excluded from the
# globs above; tagged `fixme` so `bazel test //... (-fixme)` stays green. Run it:
#   bazel test //inou/prp:prp-equiv-signed_one_bit_cmp --test_tag_filters=
sh_test(
    name = "prp-equiv-signed_one_bit_cmp",
    srcs = ["tests/pyrope_test.py"],
    args = ["-i", "$(location //inou/prp:tests/equiv/signed_one_bit_cmp.prp)"],
    data = [
        "//inou/prp:tests/prplib.py",
        "//inou/prp:tests/pyrope_test.py",
        "//inou/prp:tests/equiv/signed_one_bit_cmp.prp",
        "//inou/prp:tests/equiv/signed_one_bit_cmp.v",
        "//lhd",
    ],
    deps = ["//inou/yosys:scripts"],
    tags = ["fixme"],
)

# Known-failing equiv tracker (fixme): gen_type_cast.prp is a DELIBERATELY-broken
# generics repro — the `T(a)` constructor on a generic parameter does not lower
# yet (the binding pass leaves it as a call to `inl1_T`; see the file header,
# tracked by 3g). The .prp cannot compile, so the forward equiv pair can never
# pass. Excluded from the glob above; tagged `fixme` so `bazel test //... (-fixme)`
# stays green. Run it: bazel test //inou/prp:prp-equiv-gen_type_cast --test_tag_filters=
sh_test(
    name = "prp-equiv-gen_type_cast",
    srcs = ["tests/pyrope_test.py"],
    args = ["-i", "$(location //inou/prp:tests/equiv/gen_type_cast.prp)"],
    data = [
        "//inou/prp:tests/prplib.py",
        "//inou/prp:tests/pyrope_test.py",
        "//inou/prp:tests/equiv/gen_type_cast.prp",
        "//inou/prp:tests/equiv/gen_type_cast.v",
        "//lhd",
    ],
    deps = ["//inou/yosys:scripts"],
    tags = ["fixme"],
)

# 2f-v2prp: the REVERSE direction over the same equiv corpus. The slang reader
# lowers each golden <name>.v to Pyrope (upass/prp_writer); `lhd lec` recompiles
# that Pyrope and proves it equivalent to the ORIGINAL hand-written <name>.prp
# (NOT the .v) via the in-process cvc5 BMC engine (the default since the
# ind->bmc flip). Proving against the .prp directly sidesteps yosys read_verilog
# gaps on the reference side (e.g. the `'{...}` assignment pattern that blocked
# packed_assign/bitrange_dyn under the old lgyosys/.v gate, both of which now
# pass). Per-side tops come from the reference's :verilog_top: (the emitted
# module) and :pyrope_top: (the original lambda) headers; a timeout is
# inconclusive (not a fail). Driven off the .v files (every pair ships one).
# 129/138 prove today.
#
# The remaining gaps still REFUTE for reasons OUTSIDE the prp_writer (the emitted
# Pyrope is correct — lgyosys proves every one of them equivalent). They are
# tagged `fixme` (the repo's known-broken convention; .bazelrc filters `-fixme`
# so `bazel test //...` stays green) with the specific root cause; flipping one
# green is the fix:
#   bazel test //inou/prp:prp-v2prp-mem_rom --test_tag_filters=
_V2PRP_FIXME = [
    # cvc5 mem-cut encoder: a memory read's latency / init contents / write
    # semantics differ from the slang-emitted reconstruction (lgyosys proves all
    # four equivalent, so the emitted Pyrope is correct — the gap is the SMT
    # memory model). Tracked on the lec roadmap (todo/livehd/lec.html, M4 arrays).
    "mem_const_idx",
    "mem_mut_array",
    "mem_pending_sync_init",
    "mem_rom",
    # multi-module hierarchical recompile: the writer emits one .prp per module
    # (apat_top + apat_enc + apat_split); reading them back as a library
    # (`pyrope:DIR/`) hits a create_io assertion on the `'{...}` unpacked-array
    # output-port wiring. The FORWARD prp-equiv-assign_pattern passes.
    "assign_pattern",
    # the original .prp is a DELIBERATELY-broken generics repro (the `T(a)`
    # constructor does not lower — see the file header); the emitted Pyrope is
    # correct, but the reference itself will not compile, so it cannot be the LEC
    # ref. Tracked by 3g (generics).
    "gen_type_cast",
    # known 1-bit signed ordered-compare fixme tracker (the same gap the forward
    # prp-equiv side carries).
    "signed_one_bit_cmp",
]

[
    sh_test(
        name = "prp-v2prp-%s" % base,
        srcs = ["tests/v2prp_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:tests/equiv/%s.v)" % base,
        ],
        # The harness LECs the emitted Pyrope against the original .prp, so BOTH
        # the golden .v (the input) and the .prp (the reference) are runfiles.
        data = [
            "//inou/prp:tests/v2prp_test.py",
            "//inou/prp:tests/equiv/%s.v" % base,
            "//inou/prp:tests/equiv/%s.prp" % base,
            "//lhd",
        ],
        tags = ["fixme"] if base in _V2PRP_FIXME else [],
        deps = ["//inou/yosys:scripts"],
    )
    # Every golden ships a matching .prp except mclk_sram_2wclk (its own fixme
    # target below — multi-clock memory has no Pyrope reference yet).
    for base in [
        f.split("/")[-1][:-2]
        for f in glob(
            ["tests/equiv/*.v"],
            exclude = ["tests/equiv/mclk_sram_2wclk.v"],
        )
    ]
]

# 2w-prp_writer bucket C2 (DEFERRED to 2m-multiport_mem): a multi-clock SRAM
# whose two write ports use different clocks. This aborts in the FRONT END
# (upass.attributes: `clock_pin_refname` of the memory is add-only), never
# reaching the writer — it needs a per-port memory-clock representation +
# cgen's existing ware/rtl/cgen_memory_multiclock_*.v wrapper wiring. It is also
# the ONE golden with no <name>.prp reference, so the v2prp harness has nothing
# to LEC against either. Tagged `fixme` (repo known-broken convention; .bazelrc
# filters `-fixme` so `bazel test //...` stays green):
#   bazel test //inou/prp:prp-v2prp-mclk_sram_2wclk --test_tag_filters=
sh_test(
    name = "prp-v2prp-mclk_sram_2wclk",
    srcs = ["tests/v2prp_test.py"],
    args = [
        "-i",
        "$(location //inou/prp:tests/equiv/mclk_sram_2wclk.v)",
    ],
    data = [
        "//inou/prp:tests/v2prp_test.py",
        "//inou/prp:tests/equiv/mclk_sram_2wclk.v",
        "//lhd",
    ],
    tags = ["fixme"],
    deps = ["//inou/yosys:scripts"],
)

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
