# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# D4 EVIDENCE TOOLING. `emission_fanout.py` answers "what does this changed
# register actually reach" over a cgen netlist, and the tests that rely on it
# must get it as a declared RUNFILE: `no-sandbox` is a sandboxing choice, not a
# runfile declaration, and a shell test that cannot find the analyzer exits
# `FAIL: cannot find emission_fanout.py` rather than quietly skipping the leg.

load("@rules_shell//shell:sh_test.bzl", "sh_test")

filegroup(
    name = "emission_fanout",
    srcs = ["emission_fanout.py"],
    visibility = ["//visibility:public"],
)

exports_files(["emission_fanout.py"])

# THE ANALYZER'S OWN FAIL-CLOSED RULE, as a test. Its one contract is that a
# DEAD verdict can never come from a parse miss, and it broke that on the very
# first form review tried: `assign out = seed;` parsed its LHS as a net called
# "assign out", so the module's real output was never reached and a live seed
# came back DEAD. Every supported assignment form must preserve the ACTUAL
# left-hand side; every unsupported one must report UNPARSED.
sh_test(
    name = "emission_fanout_test",
    size = "small",
    srcs = ["emission_fanout_test.sh"],
    data = [":emission_fanout"],
)
