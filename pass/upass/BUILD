# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_upass",
    srcs = glob(
        ["*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//lnast",
        "//pass/common:pass",
        "//upass/assert:upass_assert",
        "//upass/attributes:upass_attributes",
        "//upass/bitwidth:upass_bitwidth",
        "//upass/coalescer:upass_coalescer",
        "//upass/constprop:upass_constprop",
        "//upass/core:upass",
        "//upass/cyclecmd:upass_cyclecmd",
        "//upass/func_extract:upass_func_extract",
        "//upass/noop:upass_noop",
        "//upass/pipe:upass_pipe",
        "//upass/runner:upass_runner",
        "//upass/semacheck:upass_semacheck",
        "//upass/ssa:upass_ssa",
        "//upass/tolg:upass_tolg",
        "//upass/typecheck:upass_typecheck",
        "//upass/verifier:upass_verifier",
    ],
    alwayslink = True,  # Needed to have constructor called
)

# Driven through the lhd kernel (--set upass.flag=value; uPass diagnostics
# come from the per-step log under --workdir).
_LNAST_UPASS_TESTS = [
    "upass_dependency_test.sh",
    "upass_noop_first_iter_test.sh",
    "upass_cycle_cmd_test.sh",
]

[
    sh_test(
        name = t,
        srcs = ["tests/%s" % t],
        data = [
            "//inou/prp:tests/pyrope/simple.prp",
            "//lhd",
        ],
    )
    for t in _LNAST_UPASS_TESTS
]


sh_test(
    name = "upass_func_extract_test.sh",
    srcs = ["tests/upass_func_extract_test.sh"],
    data = [
        "//inou/prp:tests/pyrope/simple.prp",
        "//lhd",
    ],
)

sh_test(
    name = "upass_coalescer_dse_test.sh",
    srcs = ["tests/upass_coalescer_dse_test.sh"],
    data = [
        "//lhd",
    ],
)

test_suite(
    name = "upass_smoke_suite",
    tests = [
        "//upass/runner:upass_runner_cycle_test",
        ":upass_dependency_test.sh",
        ":upass_noop_first_iter_test.sh",
        ":upass_cycle_cmd_test.sh",
        ":upass_func_extract_test.sh",
        ":upass_coalescer_dse_test.sh",
    ],
)
