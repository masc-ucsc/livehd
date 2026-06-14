# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# lhd kernel tests (task 1y-bazel):
#  - trivial_net: the //tools:lhd.bzl ruleset driving `lhd compile verilog`
#    as a hermetic genrule action.
#  - lhd_flow-<in>-to-<out>: the full (input x output) kind matrix on the
#    inou/prp/tests/equiv trivial_if pyrope/verilog golden pair. Supported
#    cells run the flow and validate the artifact; the rest lock the
#    documented `unsupported` v0 contract. verilog/pyrope inputs emit
#    lnast/pyrope as --emit-dir (variable cardinality); lgraph is always the
#    lgdb directory blob.
#  - lhd_equiv_test: prp->ln->lg->v1, prp->lg->v2, verilog0->lg->v3, then
#    LEC of each against the golden verilog0.

load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:lhd.bzl", "lhd_verilog")

lhd_verilog(
    name = "trivial_net",
    srcs = ["trivial.v"],
    out = "trivial.gen.v",
    recipe = "O1",
    top = "trivial",
)

_FLOW_DATA = [
    "//lhd",
    "//inou/prp:equiv_tests",
    "//inou/yosys:scripts",
]

# (input kind, output kind, expectation). ln: = Forest save dir (LNAST
# units), lg: = GraphLibrary save dir (LGraphs).
_FLOWS = [
    ("pyrope", "verilog", "pass"),
    ("pyrope", "pyrope", "pass"),
    ("pyrope", "ln", "pass"),
    ("pyrope", "lg", "pass"),
    ("verilog", "verilog", "pass"),
    ("verilog", "pyrope", "unsupported"),  # no LGraph -> LNAST decompiler
    ("verilog", "ln", "unsupported"),  # verilog elaborates to LGraphs
    ("verilog", "lg", "pass"),
    ("ln", "verilog", "pass"),
    ("ln", "pyrope", "pass"),
    ("ln", "ln", "pass"),
    ("ln", "lg", "pass"),
    ("lg", "verilog", "pass"),
    ("lg", "pyrope", "unsupported"),  # no LGraph -> LNAST decompiler
    ("lg", "ln", "unsupported"),  # no LGraph -> LNAST decompiler
    ("lg", "lg", "pass"),
]

[
    sh_test(
        name = "lhd_flow-{}-to-{}".format(in_kind, out_kind),
        srcs = ["lhd_flow_test.sh"],
        args = [
            in_kind,
            out_kind,
            expect,
        ],
        data = _FLOW_DATA,
    )
    for (in_kind, out_kind, expect) in _FLOWS
]

sh_test(
    name = "lhd_equiv_test",
    size = "large",
    srcs = ["lhd_equiv_test.sh"],
    data = _FLOW_DATA,
)

sh_test(
    name = "lhd_scan_test",
    srcs = ["lhd_scan_test.sh"],
    data = _FLOW_DATA + ["scan_imports.prp"],
)

# 2c-color / 2p-partition: `lhd pass color <alg>` colors a flat top, `lhd pass
# partition` rebuilds it as one-module-per-region + a new top, and the
# partitioned Verilog is LEC-equivalent to the original (acyclic + synth).
sh_test(
    name = "lhd_partition_test",
    size = "large",
    srcs = ["lhd_partition_test.sh"],
    data = _FLOW_DATA + ["part_flat.v"],
)

# 2c-color: every coloring algorithm (acyclic|synth|path|mincut) + continuous +
# clear run through the lhd CLI on a real design.
sh_test(
    name = "lhd_color_test",
    size = "large",
    srcs = ["lhd_color_test.sh"],
    data = _FLOW_DATA + ["part_hier.v"],
)

# Task 1m — pub/import machinery (the LiveHD docs): pub index
# + __pub wrapper publication, two-invocation and iterate-until-converged
# same-invocation imports, and the failure-mode diagnostics.
sh_test(
    name = "lhd_import_test",
    srcs = ["lhd_import_test.sh"],
    data = _FLOW_DATA,
)

# Task 1m-C — graph-library linker: merge lg: + ln: inputs into a new lg:
# library (one defines `foo`, another imports it as a black box).
sh_test(
    name = "lhd_lg_merge_test",
    srcs = ["lhd_lg_merge_test.sh"],
    data = _FLOW_DATA,
)

# Doc-test for the "Linking libraries (Pyrope + a Verilog black box)" section of
# the usage guide (../docs/docs/livehd/02-usage.md): yosys leaf + Pyrope leaf +
# Pyrope top → linked lg → Verilog.
sh_test(
    name = "lhd_usage_merge_test",
    srcs = ["lhd_usage_merge_test.sh"],
    data = _FLOW_DATA + glob(["merge_demo/*"]),
)

# --reader yosys-verilog|yosys-slang|slang trichotomy (slang = direct
# inou.slang SV->LNAST front-end: ln:/pyrope: only until tolg can lower it).
sh_test(
    name = "lhd_reader_test",
    srcs = ["lhd_reader_test.sh"],
    data = _FLOW_DATA + ["trivial.v"],
)

# --config lhd.toml pass-flag defaults (strict TOML subset; CLI flags win).
sh_test(
    name = "lhd_config_test",
    srcs = ["lhd_config_test.sh"],
    data = _FLOW_DATA,
)

# ECMA-426 source-map egress ([[1f]]-G): .v.map sidecar with sourcesContent,
# in-process and lg:-reload (hash-validated disk recovery + drift rejection).
sh_test(
    name = "lhd_srcmap_test",
    srcs = ["lhd_srcmap_test.sh"],
    data = _FLOW_DATA,
)

# --dump parse|lnast|lg screen debug observables (stderr; stage-forcing).
sh_test(
    name = "lhd_dump_test",
    srcs = ["lhd_dump_test.sh"],
    data = _FLOW_DATA,
)

# --diag-fmt auto|jsonl|pretty: stdout envelope + stderr diagnostic rendering
# (auto = isatty(stdout): pretty on a terminal, jsonl when piped/captured).
sh_test(
    name = "lhd_diag_fmt_test",
    srcs = ["lhd_diag_fmt_test.sh"],
    data = _FLOW_DATA,
)

# ln.cat / ln.diff LNAST debug tools (stdout payload; hhds tree edit distance).
sh_test(
    name = "lhd_ln_tools_test",
    srcs = ["lhd_ln_tools_test.sh"],
    data = _FLOW_DATA,
)

# `lhd list options [REGEX]` / `lhd describe pass.flag` (the --set/--config
# vocabulary from the EPRP label registry) + strict --set/--config validation
# (typo'd pass or flag = usage error, never a silent no-op).
sh_test(
    name = "lhd_list_options_test",
    srcs = ["lhd_list_options_test.sh"],
    data = _FLOW_DATA,
)

# --recipe O2 (pass.cprop + pass.bitwidth): cell-type mix LEC'd against the
# source, recipe provenance in --result-json, and the memory sizing path.
sh_test(
    name = "lhd_bitwidth_o2_test",
    size = "large",
    srcs = ["lhd_bitwidth_o2_test.sh"],
    data = _FLOW_DATA + [
        "bw_mem.v",
        "bw_mix.v",
    ],
)

# --recipe O1 (pass.cprop): a runtime Set_mask write chain plus single-bit
# reads must fold through Cprop::try_find_single_driver_pin (the recursive
# single-driver resolver) so y0->a and y2->c in the generated Verilog.
sh_test(
    name = "lhd_setmask_bitread_test",
    srcs = ["lhd_setmask_bitread_test.sh"],
    data = _FLOW_DATA + ["setmask_bitread.prp"],
)

# `--emit-dir pyrope:DIR` re-emission of a construct-rich source: the
# coalescer upass per-op hooks (toln consumer present) + pass.prp_writer
# unit emission, with the verifier discharging every cassert.
sh_test(
    name = "lhd_prp_writer_test",
    srcs = ["lhd_prp_writer_test.sh"],
    data = _FLOW_DATA + ["writer_rich.prp"],
)

# `lhd lsp` scripted JSON-RPC session over stdio: initialize negotiation,
# didOpen/didChange/didSave/didClose, pull diagnostics on a broken buffer,
# MethodNotFound, and clean shutdown/exit.
sh_test(
    name = "lhd_lsp_test",
    srcs = ["lhd_lsp_test.py"],
    data = ["//lhd"],
)

# Kernel debug observables: --dump lg (multi-graph stderr dump), --verbose
# (step-log mirroring), --depfile (make-style prerequisites).
sh_test(
    name = "lhd_debug_obs_test",
    size = "large",
    srcs = ["lhd_debug_obs_test.sh"],
    data = _FLOW_DATA + ["bw_mix.v"],
)
