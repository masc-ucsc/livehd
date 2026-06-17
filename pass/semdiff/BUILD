# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

# Reusable engine: structural_match(a, b) stamps the `match` attribute. Free of
# the cvc5-heavy pass/lec dep (the name normalization is replicated locally), so
# `lec` can call it cheaply to pre-match before building its miter (v2).
cc_library(
    name = "semdiff",
    srcs = ["semdiff.cpp"],
    hdrs = ["semdiff.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//graph",
    ],
)

# Plugin: pass.semdiff (the semdiff.* set-option registry + pass entry point).
cc_library(
    name = "pass_semdiff",
    srcs = ["pass_semdiff.cpp"],
    hdrs = ["pass_semdiff.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":semdiff",
        "//core",
        "//pass/common:pass",
    ],
    alwayslink = True,  # static Pass_plugin registration
)

cc_test(
    name = "semdiff_test",
    srcs = ["semdiff_test.cpp"],
    copts = COPTS,
    deps = [
        ":semdiff",
        "//core",
        "//graph",
        "@googletest//:gtest_main",
    ],
)
