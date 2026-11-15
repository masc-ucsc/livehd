# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")
load("//tools:regression.bzl", "cc_test")

# The tree aegraph (todo 2a-treeaeg): the aegraph rules on the staged LNAST,
# run by uPass_runner between the walk and the post-walk DCE (pass.upass
# injects rewrite_staging, so upass_runner itself does not link the engine).
cc_library(
    name = "upass_aeg",
    srcs = [
        "aeg_map.cpp",
        "aeg_rules.cpp",
        "upass_aeg.cpp",
    ],
    hdrs = [
        "aeg_map.hpp",
        "aeg_rules.hpp",
        "upass_aeg.hpp",
    ],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
        "//lnast",
        "//pass/aegraph:fold_engine",
        "//pass/aegraph:pass_aegraph",
        "//upass/core:upass",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:inlined_vector",
    ],
)

# Rule partition, prescan, scoping, write-back, refusals, validity,
# idempotence/determinism, linear scale, and the runner integration (the step
# injected into uPass_runner, then its DCE in mark and rebuild mode).
cc_test(
    name = "upass_aeg_test",
    size = "medium",
    srcs = ["upass_aeg_test.cpp"],
    copts = COPTS,
    deps = [
        ":upass_aeg",
        "//lnast",
        "//pass/aegraph:pass_aegraph",
        "//upass/attributes:upass_attributes",
        "//upass/constprop:upass_constprop",
        "//upass/core:upass",
        "//upass/runner:upass_runner",
        "//upass/typecheck:upass_typecheck",
        "@googletest//:gtest_main",
    ],
)

# The import table against what lnast.tolg builds: every mapped op over
# constants, folded both ways, plus the cell and sink pid per operand.
cc_test(
    name = "upass_aeg_conformance_test",
    srcs = ["upass_aeg_conformance_test.cpp"],
    copts = COPTS,
    deps = [
        ":upass_aeg",
        "//core",
        "//graph",
        "//lnast",
        "//pass/aegraph:pass_aegraph",
        "//upass/tolg:upass_tolg",
        "@googletest//:gtest_main",
    ],
)
