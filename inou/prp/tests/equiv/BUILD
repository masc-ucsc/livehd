# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Pyrope/Verilog equivalence tests.  Each `<name>.prp` + `<name>.v` pair under
# this directory describes the same module; equiv_test.sh runs the pair through
# the full pipeline (inou.prp |> pass.upass |> pass.lnast_to_lgraph |>
# inou.cgen.verilog) and compares the generated Verilog against the reference
# with inou/yosys/lgcheck.

load("@rules_shell//shell:sh_test.bzl", "sh_test")

exports_files([
    "trivial.prp",
    "trivial.v",
    "equiv_test.sh",
])

# One sh_test per .prp/.v pair.  We start with `trivial`; add more entries as
# they pass.
sh_test(
    name = "equiv_trivial",
    srcs = ["equiv_test.sh"],
    args = ["trivial"],
    data = [
        "trivial.prp",
        "trivial.v",
        "//inou/yosys:lgcheck",
        "//inou/yosys:yosys2",
        "//main:lgshell",
    ],
    tags = ["fixme"],  # remove once trivial is consistently green
)
