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
        "//upass/constprop:upass_constprop",
        "//upass/core:upass",
        "//upass/cyclecmd:upass_cyclecmd",
        "//upass/cyclecmd_lgraph:upass_cyclecmd_lgraph",
        "//upass/noop:upass_noop",
        "//upass/runner:upass_runner",
        "//upass/verifier:upass_verifier",
    ],
    alwayslink = True,  # Needed to have constructor called
)

sh_test(
    name = "upass_converge_test.sh",
    srcs = ["tests/upass_converge_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_dependency_test.sh",
    srcs = ["tests/upass_dependency_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_unknown_and_maxiters_test.sh",
    srcs = ["tests/upass_unknown_and_maxiters_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_iteration_diag_test.sh",
    srcs = ["tests/upass_iteration_diag_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_noop_first_iter_test.sh",
    srcs = ["tests/upass_noop_first_iter_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_cycle_cmd_test.sh",
    srcs = ["tests/upass_cycle_cmd_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_maxiters_zero_test.sh",
    srcs = ["tests/upass_maxiters_zero_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_order_parse_test.sh",
    srcs = ["tests/upass_order_parse_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_invalid_ir_test.sh",
    srcs = ["tests/upass_invalid_ir_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lnast_shared_scan_test.sh",
    srcs = ["tests/upass_lnast_shared_scan_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lnast_shared_decide_test.sh",
    srcs = ["tests/upass_lnast_shared_decide_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lgraph_mode_test.sh",
    srcs = ["tests/upass_lgraph_mode_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lgraph_shared_decide_test.sh",
    srcs = ["tests/upass_lgraph_shared_decide_test.sh"],
    data = [
        ":tests/lgraph_shift_div.prp",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lgraph_missing_input_test.sh",
    srcs = ["tests/upass_lgraph_missing_input_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lgraph_mutation_test.sh",
    srcs = ["tests/upass_lgraph_mutation_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lgraph_cycle_cmd_test.sh",
    srcs = ["tests/upass_lgraph_cycle_cmd_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lgraph_unknown_test.sh",
    srcs = ["tests/upass_lgraph_unknown_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lgraph_semantic_fold_test.sh",
    srcs = ["tests/upass_lgraph_semantic_fold_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lgraph_neutral_fold_test.sh",
    srcs = ["tests/upass_lgraph_neutral_fold_test.sh"],
    data = [
        ":tests/lgraph_neutral.prp",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lgraph_inherit_false_test.sh",
    srcs = ["tests/upass_lgraph_inherit_false_test.sh"],
    data = [
        ":tests/lgraph_shift_div.prp",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lgraph_label_carryover_test.sh",
    srcs = ["tests/upass_lgraph_label_carryover_test.sh"],
    data = [
        ":tests/lgraph_shift_div.prp",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lgraph_dry_run_test.sh",
    srcs = ["tests/upass_lgraph_dry_run_test.sh"],
    data = [
        ":tests/lgraph_shift_div.prp",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_cross_ir_pipeline_test.sh",
    srcs = ["tests/upass_cross_ir_pipeline_test.sh"],
    data = [
        ":tests/lgraph_shift_div.prp",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_cross_ir_inherit_false_test.sh",
    srcs = ["tests/upass_cross_ir_inherit_false_test.sh"],
    data = [
        ":tests/lgraph_shift_div.prp",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lgraph_shift_div_fold_test.sh",
    srcs = ["tests/upass_lgraph_shift_div_fold_test.sh"],
    data = [
        ":tests/lgraph_shift_div.prp",
        "//main:lgshell",
    ],
)

sh_test(
    name = "upass_lgraph_sub_fold_test.sh",
    srcs = ["tests/upass_lgraph_sub_fold_test.sh"],
    data = [
        ":tests/lgraph_sub.prp",
        "//main:lgshell",
    ],
)

test_suite(
    name = "upass_smoke_suite",
    tests = [
        "//upass/runner:upass_runner_cycle_test",
        ":upass_converge_test.sh",
        ":upass_dependency_test.sh",
        ":upass_iteration_diag_test.sh",
        ":upass_noop_first_iter_test.sh",
        ":upass_cycle_cmd_test.sh",
        ":upass_maxiters_zero_test.sh",
        ":upass_order_parse_test.sh",
        ":upass_invalid_ir_test.sh",
        ":upass_lnast_shared_scan_test.sh",
        ":upass_lnast_shared_decide_test.sh",
        ":upass_lgraph_mode_test.sh",
        ":upass_lgraph_shared_decide_test.sh",
        ":upass_lgraph_missing_input_test.sh",
        ":upass_lgraph_mutation_test.sh",
        ":upass_lgraph_cycle_cmd_test.sh",
        ":upass_lgraph_unknown_test.sh",
        ":upass_lgraph_semantic_fold_test.sh",
        ":upass_lgraph_neutral_fold_test.sh",
        ":upass_lgraph_inherit_false_test.sh",
        ":upass_lgraph_label_carryover_test.sh",
        ":upass_lgraph_dry_run_test.sh",
        ":upass_cross_ir_pipeline_test.sh",
        ":upass_cross_ir_inherit_false_test.sh",
        ":upass_lgraph_shift_div_fold_test.sh",
        ":upass_lgraph_sub_fold_test.sh",
        ":upass_unknown_and_maxiters_test.sh",
    ],
)
