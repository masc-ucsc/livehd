# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//tools:aegraph.bzl", "aeg_rules")
load("//tools:copt_default.bzl", "COPTS")

# The constant-expression builtins of the rule DSL: header only, shared by rulegen, the generated rule
# code and the generated tests, so a guard means the same thing in all three.
cc_library(
    name = "rule_expr",
    hdrs = ["rulegen/rule_expr.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
)

# The rule DSL: reader, parser, lints and emitters. No dependency on the engine.
cc_library(
    name = "rulegen_lib",
    srcs = [
        "rulegen/cost_model.cpp",
        "rulegen/rules_emit.cpp",
        "rulegen/rules_parse.cpp",
        "rulegen/sexp.cpp",
    ],
    hdrs = [
        "rulegen/cost_model.hpp",
        "rulegen/rules.hpp",
        "rulegen/sexp.hpp",
    ],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":rule_expr",
        "//graph",
    ],
)

# The host tool behind aeg_rules() (tools/aegraph.bzl).
cc_binary(
    name = "rulegen",
    srcs = ["rulegen/rulegen_main.cpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":rulegen_lib"],
)

# The process-wide fold-engine setting (classic|aegraph|aegraph+classic), the cache-salt mix and the
# per-site counters. No dependency on the engine or on cprop, so cache code can key on it.
cc_library(
    name = "fold_engine",
    srcs = ["fold_engine.cpp"],
    hdrs = ["fold_engine.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "pass_aegraph",
    srcs = glob(
        [
            "*.cpp",
            "runtime/*.cpp",
            "rules/*.cpp",
        ],
        exclude = [
            "*test*.cpp",
            "*smoke*.cpp",
            "fold_engine.cpp",
        ],
    ) + [":aeg_rules_cpp"],
    hdrs = glob(
        [
            "*.hpp",
            "runtime/*.hpp",
        ],
        exclude = ["fold_engine.hpp"],
    ),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":fold_engine",
        ":rule_expr",
        ":rulegen_lib",
        "//graph",
        "//core",
        "//pass/common:pass",
        "//pass/cprop:pass_cprop",
        "@abseil-cpp//absl/functional:function_ref",
    ],
    alwayslink = True,
)

# rules/<group>.rules -> rule code (into pass_aegraph), per-group fuzz and proof tests, RULES.md.
# Adding a rule is one entry in a .rules file; see tools/aegraph.bzl and RULES.md.
aeg_rules(
    name = "aeg_rules",
    fuzz_deps = [
        ":pass_aegraph",
        ":rule_test_support",
        "@googletest//:gtest_main",
    ],
    costs = [
        "synth",
        "sim",
    ],
    groups = [
        "arith",
        "div",
        "mask",
        "mux",
        "shift",
    ],
    native_srcs = glob(["rules/*.cpp"]) + ["runtime/analyses.cpp"],
    prove_deps = [
        ":pass_aegraph",
        ":rule_test_support",
        "@googletest//:gtest_main",
    ],
    rulegen = ":rulegen",
)

# Test support of the rule DSL: the term evaluator, the fuzz, the example builder and the bv proof
# driver behind the generated aeg_fuzz_<group>_smoke / aeg_prove_<group>_smoke.
cc_library(
    name = "rule_test_support",
    testonly = True,
    srcs = ["rulegen/rule_test_support.cpp"],
    hdrs = ["rulegen/rule_test_support.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":pass_aegraph",
        ":rule_expr",
        ":rulegen_lib",
        "//core",
        "//graph",
        "//pass/lec:lec_query",
        "@googletest//:gtest",
        "@hhds//hhds:graph",
    ],
)

cc_test(
    name = "cost_model_test",
    srcs = ["rulegen/cost_model_test.cpp"],
    copts = COPTS,
    deps = [
        ":pass_aegraph",
        ":rule_test_support",
        ":rulegen_lib",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "rules_parse_test",
    srcs = ["rulegen/rules_parse_test.cpp"],
    copts = COPTS,
    deps = [
        ":rule_test_support",
        ":rulegen_lib",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fold_body_test",
    srcs = ["fold_body_test.cpp"],
    copts = COPTS,
    deps = [
        ":fold_engine",
        ":pass_aegraph",
        "//core",
        "//graph",
        "//pass/cprop:pass_cprop",
        "//pass/lec:lec_query",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)

cc_test(
    name = "aegraph_test",
    srcs = ["aegraph_test.cpp"],
    copts = COPTS,
    deps = [
        ":pass_aegraph",
        "//core",
        "//graph",
        "//pass/cprop:pass_cprop",
        "//pass/lec:lec_query",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)

cc_test(
    name = "aeg_analyses_smoke",
    srcs = ["aeg_analyses_smoke.cpp"],
    copts = COPTS,
    data = ["rules/analyses.decl"],
    deps = [
        ":pass_aegraph",
        ":rule_test_support",
        ":rulegen_lib",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "aeg_parity_smoke",
    srcs = ["aeg_parity_smoke.cpp"],
    copts = COPTS,
    deps = [
        ":pass_aegraph",
        "//core",
        "//graph",
        "//pass/bitwidth:pass_bitwidth",
        "//pass/cprop:cprop_test_eval",
        "//pass/cprop:pass_cprop",
        "//pass/enableopt:pass_enableopt",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)

cc_test(
    name = "aeg_diff_smoke",
    srcs = ["aeg_diff_smoke.cpp"],
    copts = COPTS,
    deps = [
        ":pass_aegraph",
        "//core",
        "//graph",
        "//pass/cprop:cprop_test_eval",
        "//pass/cprop:pass_cprop",
        "//pass/lec:lec_query",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)

cc_test(
    name = "aeg_union_smoke",
    srcs = ["aeg_union_smoke.cpp"],
    copts = COPTS,
    deps = [
        ":pass_aegraph",
        "//core",
        "//graph",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)
