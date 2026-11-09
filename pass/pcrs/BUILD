load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")
load("//tools:regression.bzl", "cc_test")

cc_library(
    name = "pcrs",
    srcs = ["pcrs.cpp"],
    hdrs = ["pcrs.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
        "//inou/lefdef:inou_lefdef",
        "//pass/liberty:liberty_dff",
    ],
)

cc_test(
    name = "pcrs_test",
    size = "small",
    srcs = ["pcrs_test.cpp"],
    copts = COPTS,
    deps = [
        ":pcrs",
        "@googletest//:gtest_main",
    ],
)
