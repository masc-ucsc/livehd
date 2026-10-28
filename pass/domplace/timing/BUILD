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

# The same cells as one Liberty library, for engines that read Liberty only
# (OpenTimer: placement resizing, buffer repair, timing-driven placement).
cc_library(
    name = "domino_liberty",
    srcs = ["domino_liberty.cpp"],
    hdrs = ["domino_liberty.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":domino_timing_model",
        "//pass/liberty:timing_model",
    ],
)

# The code identity of stage timing: part of the physical cache key only when
# domino sizing optimizes against it (report-only timing never re-places).
genrule(
    name = "stage_timing_salt",
    srcs = glob(
        ["*.cpp", "*.hpp"],
        exclude = ["*_test.cpp", "*_fixture.hpp"],
    ),
    outs = ["stage_timing_salt.hpp"],
    cmd = "h=$$(cat $(SRCS) | (shasum -a 256 2>/dev/null || sha256sum) | cut -c1-16); " +
          "printf '#pragma once\\n#include <cstdint>\\nnamespace livehd::domplace::timing { inline constexpr uint64_t kStageTimingSalt = 0x%sULL; }\\n' \"$$h\" > $@",
)

# Constraints, half-cycle stage timing and the stage_timing.json report.
cc_library(
    name = "stage_timing",
    srcs = ["stage_timing.cpp"],
    hdrs = [
        "stage_timing.hpp",
        ":stage_timing_salt",
    ],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":domino_timing_model",
        "//inou/lefdef:inou_lefdef",
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

cc_test(
    name = "domino_liberty_test",
    size = "small",
    srcs = ["domino_liberty_test.cpp"],
    copts = COPTS,
    deps = [
        ":domino_liberty",
        ":domino_timing_fixture",
        "@googletest//:gtest_main",
    ],
)
