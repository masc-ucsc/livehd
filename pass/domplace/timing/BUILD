# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

# Provisional domino stage timing (todo/livehd/2d-domplace.html, E). A
# separate package keeps it outside the //pass/domplace:domplace_salt glob:
# editing the timing model never changes a published bundle or its caches.

# One characterized provisional cell per bundle variant for the native proxy.
cc_library(
    name = "domino_timing_model",
    srcs = ["domino_timing_model.cpp"],
    hdrs = ["domino_timing_model.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//pass/domplace:contract",
        "//pass/domplace:domino_bundle",
        "//pass/liberty:timing_model",
        "//pass/place:late_timing",
        "@rapidjson",
    ],
)

# Constraints, half-cycle stage timing and the stage_timing.json report.
cc_library(
    name = "stage_timing",
    srcs = ["stage_timing.cpp"],
    hdrs = ["stage_timing.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":domino_timing_model",
        "//pass/domplace:contract",
        "//pass/place:late_timing",
        "//pass/place:require",
        "@rapidjson",
    ],
)

cc_library(
    name = "domino_timing_fixture",
    testonly = True,
    hdrs = ["domino_timing_fixture.hpp"],
    copts = COPTS,
    deps = [
        "//inou/lefdef:inou_lefdef",
        "//pass/domplace:domino_bundle",
        "//pass/place:timing_circuit_fixture",
        "@rapidjson",
    ],
)

cc_test(
    name = "domino_timing_model_test",
    size = "small",
    srcs = ["domino_timing_model_test.cpp"],
    copts = COPTS,
    deps = [
        ":domino_timing_fixture",
        ":domino_timing_model",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "stage_timing_test",
    size = "small",
    srcs = ["stage_timing_test.cpp"],
    copts = COPTS,
    deps = [
        ":domino_timing_fixture",
        ":stage_timing",
        "@googletest//:gtest_main",
        "@rapidjson",
    ],
)
