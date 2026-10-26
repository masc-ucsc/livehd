load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "period",
    srcs = ["period.cpp"],
    hdrs = ["period.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
)

cc_test(
    name = "period_test",
    size = "small",
    srcs = ["period_test.cpp"],
    copts = COPTS,
    deps = [":period", "@googletest//:gtest_main"],
)

cc_library(
    name = "proxy_bridge",
    srcs = ["proxy_bridge.cpp"],
    hdrs = ["proxy_bridge.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":period", "//pass/place:late_timing"],
)

cc_test(
    name = "proxy_bridge_test",
    size = "small",
    srcs = ["proxy_bridge_test.cpp"],
    copts = COPTS,
    deps = [":proxy_bridge", "@googletest//:gtest_main"],
)

filegroup(
    name = "timing_sources",
    srcs = glob(["*.cpp", "*.hpp"], exclude = ["*_test.cpp"]),
    visibility = ["//visibility:public"],
)
