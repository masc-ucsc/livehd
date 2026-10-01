# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# scripts/ is not built; it is exposed so TESTS can depend on the scripts they
# pin. Without this, a shell test for one of these can only be run by hand, and
# a regression that is only caught when someone remembers to run it is not a
# maintained regression.

filegroup(
    name = "cycle_provenance_tools",
    srcs = [
        "coreet_filelist.sh",
        "coreet_print.mk",
        "census_merge.py",
        "coreet_d2_census.sh",
        "coreet_d2_census_report.py",
        "coreet_lowering_diffsim.py",
        "mem_lean_diffsim.py",
        "cycle_provenance.sh",
        "lean_validate.sh",
        "lean_validate_join.py",
        "lean_local_lake.sh",
        "lec_gate_verdict.sh",
        "proc_ifx_scc_compare.sh",
        "lgraph_scc.py",
        "lgraph_cycle_stages.sh",
        "cert_reemit_diff.sh",
        "mutate_check.py",
    ] + glob(["mutation_specs/*.json"]),
    visibility = ["//visibility:public"],
)
