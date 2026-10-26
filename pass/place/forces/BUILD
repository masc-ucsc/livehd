load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pin_weights",
    srcs = ["pin_weights.cpp"],
    hdrs = ["pin_weights.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//pass/place/objective:proxy_bridge"],
)

cc_test(
    name = "pin_weights_test",
    size = "small",
    srcs = ["pin_weights_test.cpp"],
    copts = COPTS,
    deps = [":pin_weights", "@googletest//:gtest_main"],
)

cc_library(
    name = "startup",
    srcs = ["startup.cpp"],
    hdrs = ["startup.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":pin_weights"],
)
cc_test(
    name = "startup_test",
    size = "small",
    srcs = ["startup_test.cpp"],
    copts = COPTS,
    deps = [":startup", "@googletest//:gtest_main"],
)

cc_library(
    name = "gp_timing",
    srcs = ["gp_timing.cpp"],
    hdrs = ["gp_timing.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":legalization", ":pin_weights", ":startup", "//pass/place:timing_comparison"],
)

cc_library(
    name = "legalization",
    srcs = ["legalization.cpp"],
    hdrs = ["legalization.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":pin_weights"],
)

cc_test(
    name = "legalization_test",
    size = "small",
    srcs = ["legalization_test.cpp"],
    copts = COPTS,
    deps = [":legalization", "@googletest//:gtest_main"],
)

cc_test(
    name = "gp_timing_test",
    size = "small",
    srcs = ["gp_timing_test.cpp"],
    copts = COPTS,
    deps = [":gp_timing", "//pass/place:timing_circuit_fixture", "@googletest//:gtest_main"],
)

cc_library(
    name = "gp_rejection",
    srcs = ["gp_rejection.cpp"],
    hdrs = ["gp_rejection.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":gp_timing", "//core", "//pass/place:late_timing", "@rapidjson"],
)

cc_test(
    name = "gp_rejection_test",
    size = "small",
    srcs = ["gp_rejection_test.cpp"],
    copts = COPTS,
    deps = [":gp_rejection", "@googletest//:gtest_main", "@rapidjson"],
)

filegroup(
    name = "timing_sources",
    srcs = glob(["*.cpp", "*.hpp"], exclude = ["*_test.cpp"]),
    visibility = ["//visibility:public"],
)
